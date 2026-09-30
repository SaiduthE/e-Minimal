#include "JpegToFramebufferConverter.h"

#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <JPEGDEC.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>

#include "DirectPixelWriter.h"
#include "DitherUtils.h"
#include "PixelCache.h"
#include "ToneCurve.h"

namespace {

// Context struct passed through JPEGDEC callbacks to avoid global mutable state.
// The draw callback receives this via pDraw->pUser (set by setUserPointer()).
// The file I/O callbacks receive the HalFile* via pFile->fHandle (set by jpegOpen()).
struct JpegContext {
  GfxRenderer* renderer{nullptr};
  const RenderConfig* config{nullptr};
  int screenWidth{0};
  int screenHeight{0};

  // Visible (cropped) part of the source after JPEGDEC's built-in scaling, in
  // scaled-source pixels. The draw callback shifts block origins by the crop, so
  // all scaling math runs relative to the visible region's top-left corner.
  int cropLeft{0};
  int cropTop{0};
  int visibleWidth{0};
  int visibleHeight{0};

  // Final output dimensions
  int dstWidth{0};
  int dstHeight{0};

  // Fine scale in 16.16 fixed-point (ESP32-C3 has no FPU).
  // X and Y axes use separate scale factors: the aspect ratio of the output (dstWidth/dstHeight)
  // may differ from the source (srcWidth/srcHeight) due to integer rounding of displayHeight.
  // Using a single (X-based) scale for both axes causes the wrong srcRow to be skipped
  // during nearest-neighbor downscaling, potentially losing critical image content.
  int32_t fineScaleFPX{1 << 16};  // X: src -> dst column mapping
  int32_t invScaleFPX{1 << 16};   // X: dst -> src column mapping
  int32_t fineScaleFPY{1 << 16};  // Y: src -> dst row mapping
  int32_t invScaleFPY{1 << 16};   // Y: dst -> src row mapping

  PixelCache cache;
  bool caching{false};

  // Photo path (fullGrayLevels / autoContrast); nullptr: the default 2bpp path.
  const image_tone::TonedSink* toned{nullptr};

  uint32_t lastYieldMs{0};  // throttle state for yieldDuringDecode()
};

// Auto-contrast pre-pass: a 1/8-scale grey decode of the visible crop that only
// counts grey levels.
struct JpegHistogramContext {
  uint32_t* histogram{nullptr};
  int cropLeft{0};
  int cropTop{0};
  int visibleWidth{0};
  int visibleHeight{0};
  uint32_t lastYieldMs{0};
};

// File I/O callbacks use pFile->fHandle to access the HalFile*,
// avoiding the need for global file state.
void* jpegOpen(const char* filename, int32_t* size) {
  HalFile* f = new HalFile();
  if (!Storage.openFileForRead("JPG", std::string(filename), *f)) {
    delete f;
    return nullptr;
  }
  *size = f->size();
  return f;
}

void jpegClose(void* handle) {
  HalFile* f = reinterpret_cast<HalFile*>(handle);
  if (f) {
    f->close();
    delete f;
  }
}

// JPEGDEC tracks file position via pFile->iPos internally (e.g. JPEGGetMoreData
// checks iPos < iSize to decide whether more data is available). The callbacks
// MUST maintain iPos to match the actual file position, otherwise progressive
// JPEGs with large headers fail during parsing.
int32_t jpegRead(JPEGFILE* pFile, uint8_t* pBuf, int32_t len) {
  HalFile* f = reinterpret_cast<HalFile*>(pFile->fHandle);
  if (!f) return 0;
  int32_t bytesRead = f->read(pBuf, len);
  if (bytesRead < 0) return 0;
  pFile->iPos += bytesRead;
  return bytesRead;
}

int32_t jpegSeek(JPEGFILE* pFile, int32_t pos) {
  HalFile* f = reinterpret_cast<HalFile*>(pFile->fHandle);
  if (!f) return -1;
  if (!f->seek(pos)) return -1;
  pFile->iPos = pos;
  return pos;
}

// JPEGDEC object is ~17 KB due to internal decode buffers.
// Heap-allocate on demand so memory is only used during active decode.
constexpr size_t JPEG_DECODER_APPROX_SIZE = 20 * 1024;
constexpr size_t MIN_FREE_HEAP_FOR_JPEG = JPEG_DECODER_APPROX_SIZE + 16 * 1024;

// Choose JPEGDEC's built-in scale factor for coarse downscaling.
// Returns the scale denominator (1, 2, 4, or 8) and sets jpegScaleOption.
int chooseJpegScale(float targetScale, int& jpegScaleOption) {
  if (targetScale <= 0.125f) {
    jpegScaleOption = JPEG_SCALE_EIGHTH;
    return 8;
  }
  if (targetScale <= 0.25f) {
    jpegScaleOption = JPEG_SCALE_QUARTER;
    return 4;
  }
  if (targetScale <= 0.5f) {
    jpegScaleOption = JPEG_SCALE_HALF;
    return 2;
  }
  jpegScaleOption = 0;
  return 1;
}

// RenderConfig crop fraction (total, half per side), clamped as the PNG
// converter does; NaN and negatives mean no crop.
float cropFraction(const float fraction) {
  if (!(fraction > 0.0f)) return 0.0f;
  return fraction < 0.99f ? fraction : 0.99f;
}

// A per-side source crop in scaled-source pixels, rounded to the nearest whole
// pixel and kept small enough that at least one pixel stays visible.
int scaledCrop(const int cropPixels, const int scaleDenom, const int scaledSize) {
  int crop = (cropPixels + scaleDenom / 2) / scaleDenom;
  if (2 * crop >= scaledSize) crop = (scaledSize - 1) / 2;
  return crop;
}

int jpegHistogramCallback(JPEGDRAW* pDraw) {
  JpegHistogramContext* hc = reinterpret_cast<JpegHistogramContext*>(pDraw->pUser);
  if (!hc || !hc->histogram) return 0;

  ImageToFramebufferDecoder::yieldDuringDecode(hc->lastYieldMs);

  const uint8_t* pixels = reinterpret_cast<const uint8_t*>(pDraw->pPixels);
  const int stride = pDraw->iWidth;
  const int blockX = pDraw->x - hc->cropLeft;
  const int blockY = pDraw->y - hc->cropTop;
  // Raster MCU order: once a block starts below the visible region, so do the rest.
  if (blockY >= hc->visibleHeight) return 0;

  const int x0 = blockX < 0 ? -blockX : 0;
  const int y0 = blockY < 0 ? -blockY : 0;
  const int x1 = std::min(pDraw->iWidthUsed, hc->visibleWidth - blockX);
  const int y1 = std::min(pDraw->iHeight, hc->visibleHeight - blockY);
  for (int y = y0; y < y1; y++) {
    const uint8_t* row = pixels + y * stride;
    for (int x = x0; x < x1; x++) hc->histogram[row[x]]++;
  }
  return 1;
}

// Fixed-point 16.16 arithmetic avoids software float emulation on ESP32-C3 (no FPU).
constexpr int FP_SHIFT = 16;
constexpr int32_t FP_ONE = 1 << FP_SHIFT;
constexpr int32_t FP_MASK = FP_ONE - 1;

int jpegDrawCallback(JPEGDRAW* pDraw) {
  JpegContext* ctx = reinterpret_cast<JpegContext*>(pDraw->pUser);
  if (!ctx || !ctx->config || !ctx->renderer) return 0;

  ImageToFramebufferDecoder::yieldDuringDecode(ctx->lastYieldMs);

  // In EIGHT_BIT_GRAYSCALE mode, pPixels contains 8-bit grayscale values
  // Buffer is densely packed: stride = pDraw->iWidth, valid columns = pDraw->iWidthUsed
  uint8_t* pixels = reinterpret_cast<uint8_t*>(pDraw->pPixels);
  const int stride = pDraw->iWidth;
  const int validW = pDraw->iWidthUsed;
  const int blockH = pDraw->iHeight;

  if (stride <= 0 || blockH <= 0 || validW <= 0) return 1;

  const bool useDithering = ctx->config->useDithering;
  const image_tone::TonedSink* const toned = ctx->toned;
  bool caching = ctx->caching;
  const int32_t fineScaleFPX = ctx->fineScaleFPX;
  const int32_t invScaleFPX = ctx->invScaleFPX;
  const int32_t fineScaleFPY = ctx->fineScaleFPY;
  const int32_t invScaleFPY = ctx->invScaleFPY;
  GfxRenderer& renderer = *ctx->renderer;
  const int cfgX = ctx->config->x;
  const int cfgY = ctx->config->y;
  // Block origin relative to the visible region; negative when the block starts in the crop.
  const int blockX = pDraw->x - ctx->cropLeft;
  const int blockY = pDraw->y - ctx->cropTop;

  // Determine destination pixel range covered by this source block
  const int srcYEnd = blockY + blockH;
  const int srcXEnd = blockX + validW;

  // Blocks arrive in raster MCU order, so once one starts below the visible
  // region every later one does too: stop the decode. Blocks above or beside it
  // draw nothing.
  if (blockY >= ctx->visibleHeight) return 0;
  if (srcYEnd <= 0 || srcXEnd <= 0 || blockX >= ctx->visibleWidth) return 1;

  int dstYStart = (int)((int64_t)blockY * fineScaleFPY >> FP_SHIFT);
  int dstYEnd = (srcYEnd >= ctx->visibleHeight) ? ctx->dstHeight : (int)((int64_t)srcYEnd * fineScaleFPY >> FP_SHIFT);
  int dstXStart = (int)((int64_t)blockX * fineScaleFPX >> FP_SHIFT);
  int dstXEnd = (srcXEnd >= ctx->visibleWidth) ? ctx->dstWidth : (int)((int64_t)srcXEnd * fineScaleFPX >> FP_SHIFT);
  if (dstYStart < 0) dstYStart = 0;
  if (dstXStart < 0) dstXStart = 0;

  // Pre-clamp destination ranges to screen bounds (eliminates per-pixel screen checks)
  int clampYMax = ctx->dstHeight;
  if (ctx->screenHeight - cfgY < clampYMax) clampYMax = ctx->screenHeight - cfgY;
  if (dstYStart < -cfgY) dstYStart = -cfgY;
  if (dstYEnd > clampYMax) dstYEnd = clampYMax;

  int clampXMax = ctx->dstWidth;
  if (ctx->screenWidth - cfgX < clampXMax) clampXMax = ctx->screenWidth - cfgX;
  if (dstXStart < -cfgX) dstXStart = -cfgX;
  if (dstXEnd > clampXMax) dstXEnd = clampXMax;

  if (dstYStart >= dstYEnd || dstXStart >= dstXEnd) return 1;

  // Pre-compute orientation and render-mode state once per callback invocation
  DirectPixelWriter pw;
  pw.init(renderer);

  // The cache streams to disk one MCU-row band at a time. Flushing rows below
  // this block (raster order guarantees they are final) repositions the band;
  // cacheOriginY then maps screen rows to the band-local buffer rows. If a flush
  // write fails, stop caching for the rest of this decode (and let finalize drop
  // the partial file) rather than writing past the band buffer.
  DirectCacheWriter cw;
  int cacheOriginY = 0;
  if (caching) {
    if (!ctx->cache.advanceTo(dstYStart)) {
      caching = false;
      ctx->caching = false;
    } else {
      cw.init(ctx->cache.buffer, ctx->cache.bytesPerRow, ctx->cache.bandRows, ctx->cache.originX);
      cacheOriginY = ctx->config->y + ctx->cache.bandStart;
    }
  }

  // === 1:1 fast path: no scaling math ===
  if (fineScaleFPX == FP_ONE && fineScaleFPY == FP_ONE) {
    for (int dstY = dstYStart; dstY < dstYEnd; dstY++) {
      const int outY = cfgY + dstY;
      pw.beginRow(outY);
      if (caching) cw.beginRow(outY, cacheOriginY);
      const uint8_t* row = &pixels[(dstY - blockY) * stride];
      for (int dstX = dstXStart; dstX < dstXEnd; dstX++) {
        const int outX = cfgX + dstX;
        uint8_t gray = row[dstX - blockX];
        if (toned) {
          toned->put(pw, dstX, dstY, outX, outY, gray);
          continue;
        }
        uint8_t dithered;
        if (useDithering) {
          dithered = applyBayerDither4Level(gray, outX, outY);
        } else {
          dithered = gray / 85;
          if (dithered > 3) dithered = 3;
        }
        pw.writePixel(outX, dithered);
        if (caching) cw.writePixel(outX, dithered);
      }
    }
    return 1;
  }

  // === Bilinear interpolation (upscale: fineScale > 1.0) ===
  // Smooths block boundaries that would otherwise create visible banding
  // on progressive JPEG DC-only decode (1/8 resolution upscaled to target).
  if (fineScaleFPX > FP_ONE && fineScaleFPY > FP_ONE) {
    // Pre-compute safe X range where lx0 and lx0+1 are both in [0, validW-1].
    // Only the left/right edge pixels (typically 0-2 and 1-8 respectively) need clamping.
    int safeXStart = (int)(((int64_t)blockX * fineScaleFPX + FP_MASK) >> FP_SHIFT);
    int safeXEnd = (int)((int64_t)(blockX + validW - 1) * fineScaleFPX >> FP_SHIFT);
    if (safeXStart < dstXStart) safeXStart = dstXStart;
    if (safeXEnd > dstXEnd) safeXEnd = dstXEnd;
    if (safeXStart > safeXEnd) safeXEnd = safeXStart;

    for (int dstY = dstYStart; dstY < dstYEnd; dstY++) {
      const int outY = cfgY + dstY;
      pw.beginRow(outY);
      if (caching) cw.beginRow(outY, cacheOriginY);
      const int32_t srcFyFP = dstY * invScaleFPY;
      const int32_t fy = srcFyFP & FP_MASK;
      const int32_t fyInv = FP_ONE - fy;
      int ly0 = (srcFyFP >> FP_SHIFT) - blockY;
      int ly1 = ly0 + 1;
      if (ly0 < 0) ly0 = 0;
      if (ly0 >= blockH) ly0 = blockH - 1;
      if (ly1 >= blockH) ly1 = blockH - 1;

      const uint8_t* row0 = &pixels[ly0 * stride];
      const uint8_t* row1 = &pixels[ly1 * stride];

      // Left edge (with X boundary clamping)
      for (int dstX = dstXStart; dstX < safeXStart; dstX++) {
        const int outX = cfgX + dstX;
        const int32_t srcFxFP = dstX * invScaleFPX;
        const int32_t fx = srcFxFP & FP_MASK;
        const int32_t fxInv = FP_ONE - fx;
        int lx0 = (srcFxFP >> FP_SHIFT) - blockX;
        int lx1 = lx0 + 1;
        if (lx0 < 0) lx0 = 0;
        if (lx1 < 0) lx1 = 0;
        if (lx0 >= validW) lx0 = validW - 1;
        if (lx1 >= validW) lx1 = validW - 1;

        int top = ((int)row0[lx0] * fxInv + (int)row0[lx1] * fx) >> FP_SHIFT;
        int bot = ((int)row1[lx0] * fxInv + (int)row1[lx1] * fx) >> FP_SHIFT;
        uint8_t gray = (uint8_t)((top * fyInv + bot * fy) >> FP_SHIFT);
        if (toned) {
          toned->put(pw, dstX, dstY, outX, outY, gray);
          continue;
        }

        uint8_t dithered;
        if (useDithering) {
          dithered = applyBayerDither4Level(gray, outX, outY);
        } else {
          dithered = gray / 85;
          if (dithered > 3) dithered = 3;
        }
        pw.writePixel(outX, dithered);
        if (caching) cw.writePixel(outX, dithered);
      }

      // Interior (no X boundary checks — lx0 and lx0+1 guaranteed in bounds)
      for (int dstX = safeXStart; dstX < safeXEnd; dstX++) {
        const int outX = cfgX + dstX;
        const int32_t srcFxFP = dstX * invScaleFPX;
        const int32_t fx = srcFxFP & FP_MASK;
        const int32_t fxInv = FP_ONE - fx;
        const int lx0 = (srcFxFP >> FP_SHIFT) - blockX;

        int top = ((int)row0[lx0] * fxInv + (int)row0[lx0 + 1] * fx) >> FP_SHIFT;
        int bot = ((int)row1[lx0] * fxInv + (int)row1[lx0 + 1] * fx) >> FP_SHIFT;
        uint8_t gray = (uint8_t)((top * fyInv + bot * fy) >> FP_SHIFT);
        if (toned) {
          toned->put(pw, dstX, dstY, outX, outY, gray);
          continue;
        }

        uint8_t dithered;
        if (useDithering) {
          dithered = applyBayerDither4Level(gray, outX, outY);
        } else {
          dithered = gray / 85;
          if (dithered > 3) dithered = 3;
        }
        pw.writePixel(outX, dithered);
        if (caching) cw.writePixel(outX, dithered);
      }

      // Right edge (with X boundary clamping)
      for (int dstX = safeXEnd; dstX < dstXEnd; dstX++) {
        const int outX = cfgX + dstX;
        const int32_t srcFxFP = dstX * invScaleFPX;
        const int32_t fx = srcFxFP & FP_MASK;
        const int32_t fxInv = FP_ONE - fx;
        int lx0 = (srcFxFP >> FP_SHIFT) - blockX;
        int lx1 = lx0 + 1;
        if (lx0 >= validW) lx0 = validW - 1;
        if (lx1 >= validW) lx1 = validW - 1;

        int top = ((int)row0[lx0] * fxInv + (int)row0[lx1] * fx) >> FP_SHIFT;
        int bot = ((int)row1[lx0] * fxInv + (int)row1[lx1] * fx) >> FP_SHIFT;
        uint8_t gray = (uint8_t)((top * fyInv + bot * fy) >> FP_SHIFT);
        if (toned) {
          toned->put(pw, dstX, dstY, outX, outY, gray);
          continue;
        }

        uint8_t dithered;
        if (useDithering) {
          dithered = applyBayerDither4Level(gray, outX, outY);
        } else {
          dithered = gray / 85;
          if (dithered > 3) dithered = 3;
        }
        pw.writePixel(outX, dithered);
        if (caching) cw.writePixel(outX, dithered);
      }
    }
    return 1;
  }

  // === Nearest-neighbor (downscale: fineScale < 1.0) ===
  for (int dstY = dstYStart; dstY < dstYEnd; dstY++) {
    const int outY = cfgY + dstY;
    pw.beginRow(outY);
    if (caching) cw.beginRow(outY, cacheOriginY);
    const int32_t srcFyFP = dstY * invScaleFPY;
    int ly = (srcFyFP >> FP_SHIFT) - blockY;
    if (ly < 0) ly = 0;
    if (ly >= blockH) ly = blockH - 1;
    const uint8_t* row = &pixels[ly * stride];

    for (int dstX = dstXStart; dstX < dstXEnd; dstX++) {
      const int outX = cfgX + dstX;
      const int32_t srcFxFP = dstX * invScaleFPX;
      int lx = (srcFxFP >> FP_SHIFT) - blockX;
      if (lx < 0) lx = 0;
      if (lx >= validW) lx = validW - 1;
      uint8_t gray = row[lx];
      if (toned) {
        toned->put(pw, dstX, dstY, outX, outY, gray);
        continue;
      }

      uint8_t dithered;
      if (useDithering) {
        dithered = applyBayerDither4Level(gray, outX, outY);
      } else {
        dithered = gray / 85;
        if (dithered > 3) dithered = 3;
      }
      pw.writePixel(outX, dithered);
      if (caching) cw.writePixel(outX, dithered);
    }
  }

  return 1;
}

// Auto-contrast without a PSRAM stage: close `jpeg`, run a 1/8-scale grey
// decode of the visible crop on it into the histogram (DC only for baseline
// and progressive alike, no IDCT), then reopen it for the main decode and
// return the reopen's rc. A failed pre-pass leaves the histogram empty (the
// curve is then the gamma alone). Nothing may return between the close and
// the reopen: the caller's cleanup closes whatever `jpeg` holds.
int histogramPrepassThenReopen(JPEGDEC& jpeg, const std::string& imagePath, const int srcWidth, const int srcHeight,
                               const int cropLeft, const int cropTop, image_tone::ToneMap& toneMap) {
  jpeg.close();
  const unsigned long start = millis();
  bool counted = false;
  if (jpeg.open(imagePath.c_str(), jpegOpen, jpegClose, jpegRead, jpegSeek, jpegHistogramCallback) == 1) {
    constexpr int SCALE_DENOM = 8;
    const int scaledWidth = (srcWidth + SCALE_DENOM - 1) / SCALE_DENOM;
    const int scaledHeight = (srcHeight + SCALE_DENOM - 1) / SCALE_DENOM;
    JpegHistogramContext hc;
    hc.histogram = toneMap.histogram;
    hc.cropLeft = scaledCrop(cropLeft, SCALE_DENOM, scaledWidth);
    hc.cropTop = scaledCrop(cropTop, SCALE_DENOM, scaledHeight);
    hc.visibleWidth = scaledWidth - 2 * hc.cropLeft;
    hc.visibleHeight = scaledHeight - 2 * hc.cropTop;
    hc.lastYieldMs = start;
    jpeg.setPixelType(EIGHT_BIT_GRAYSCALE);
    jpeg.setUserPointer(&hc);
    counted = jpeg.decode(0, 0, JPEG_SCALE_EIGHTH) == 1;
  }
  if (!counted) {
    LOG_ERR("JPG", "Auto-contrast pre-pass failed (err=%d), using the gamma alone", jpeg.getLastError());
    memset(toneMap.histogram, 0, sizeof(toneMap.histogram));
  }
  jpeg.close();
  LOG_DBG("JPG", "Auto-contrast pre-pass: %lu ms", millis() - start);
  return jpeg.open(imagePath.c_str(), jpegOpen, jpegClose, jpegRead, jpegSeek, jpegDrawCallback);
}

}  // namespace

bool JpegToFramebufferConverter::getDimensionsStatic(const std::string& imagePath, ImageDimensions& out) {
  size_t freeHeap = ESP.getFreeHeap();
  if (freeHeap < MIN_FREE_HEAP_FOR_JPEG) {
    LOG_ERR("JPG", "Not enough heap for JPEG decoder (%u free, need %u)", freeHeap, MIN_FREE_HEAP_FOR_JPEG);
    return false;
  }

  std::unique_ptr<JPEGDEC> jpeg(new (std::nothrow) JPEGDEC());
  if (!jpeg) {
    LOG_ERR("JPG", "Failed to allocate JPEG decoder for dimensions");
    return false;
  }

  int rc = jpeg->open(imagePath.c_str(), jpegOpen, jpegClose, jpegRead, jpegSeek, nullptr);
  const ScopedCleanup cleanup{[&jpeg]() { jpeg->close(); }};
  if (rc != 1) {
    LOG_ERR("JPG", "Failed to open JPEG for dimensions (err=%d): %s", jpeg->getLastError(), imagePath.c_str());
    return false;
  }

  const int width = jpeg->getWidth();
  const int height = jpeg->getHeight();
  if (!validateAndStoreDimensions(width, height, out, "JPEG")) return false;
  LOG_DBG("JPG", "Image dimensions: %dx%d", width, height);

  return true;
}

bool JpegToFramebufferConverter::decodeToFramebuffer(const std::string& imagePath, GfxRenderer& renderer,
                                                     const RenderConfig& config) {
  LOG_DBG("JPG", "Decoding JPEG: %s", imagePath.c_str());

  size_t freeHeap = ESP.getFreeHeap();
  if (freeHeap < MIN_FREE_HEAP_FOR_JPEG) {
    LOG_ERR("JPG", "Not enough heap for JPEG decoder (%u free, need %u)", freeHeap, MIN_FREE_HEAP_FOR_JPEG);
    return false;
  }

  std::unique_ptr<JPEGDEC> jpeg(new (std::nothrow) JPEGDEC());
  if (!jpeg) {
    LOG_ERR("JPG", "Failed to allocate JPEG decoder");
    return false;
  }

  JpegContext ctx;
  ctx.renderer = &renderer;
  ctx.config = &config;
  ctx.screenWidth = renderer.getScreenWidth();
  ctx.screenHeight = renderer.getScreenHeight();

  int rc = jpeg->open(imagePath.c_str(), jpegOpen, jpegClose, jpegRead, jpegSeek, jpegDrawCallback);
  const ScopedCleanup cleanup{[&jpeg]() { jpeg->close(); }};
  if (rc != 1) {
    LOG_ERR("JPG", "Failed to open JPEG (err=%d): %s", jpeg->getLastError(), imagePath.c_str());
    return false;
  }

  ImageDimensions sourceDimensions;
  if (!validateAndStoreDimensions(jpeg->getWidth(), jpeg->getHeight(), sourceDimensions, "JPEG")) return false;
  const int srcWidth = sourceDimensions.width;
  const int srcHeight = sourceDimensions.height;

  // Whole source pixels cropped from each side; the fit and the coarse scale
  // below are computed on what remains visible.
  const int cropLeft = static_cast<int>(srcWidth * cropFraction(config.sourceCropX) / 2.0f);
  const int cropTop = static_cast<int>(srcHeight * cropFraction(config.sourceCropY) / 2.0f);
  const int visibleWidth = srcWidth - 2 * cropLeft;
  const int visibleHeight = srcHeight - 2 * cropTop;
  if (visibleWidth <= 0 || visibleHeight <= 0) {
    LOG_ERR("JPG", "JPEG crop leaves no visible pixels");
    return false;
  }

  bool isProgressive = jpeg->getJPEGType() == JPEG_MODE_PROGRESSIVE;
  if (isProgressive) {
    LOG_INF("JPG", "Progressive JPEG detected - decoding DC coefficients only (lower quality)");
  }

  // Calculate overall target scale
  float targetScale;
  int destWidth, destHeight;

  if (config.useExactDimensions && config.maxWidth > 0 && config.maxHeight > 0) {
    destWidth = config.maxWidth;
    destHeight = config.maxHeight;
    targetScale = (float)destWidth / visibleWidth;
  } else {
    float scaleX =
        (config.maxWidth > 0 && visibleWidth > config.maxWidth) ? (float)config.maxWidth / visibleWidth : 1.0f;
    float scaleY =
        (config.maxHeight > 0 && visibleHeight > config.maxHeight) ? (float)config.maxHeight / visibleHeight : 1.0f;
    targetScale = (scaleX < scaleY) ? scaleX : scaleY;
    if (targetScale > 1.0f) targetScale = 1.0f;

    destWidth = (int)(visibleWidth * targetScale);
    destHeight = (int)(visibleHeight * targetScale);
  }

  // Choose JPEGDEC built-in scaling for coarse downscaling.
  // Progressive JPEGs: JPEGDEC forces JPEG_SCALE_EIGHTH internally (DC-only
  // decode produces 1/8 resolution). We must match this to avoid the if/else
  // priority chain in DecodeJPEG selecting a different scale.
  int jpegScaleOption;
  int jpegScaleDenom;
  if (isProgressive) {
    jpegScaleOption = JPEG_SCALE_EIGHTH;
    jpegScaleDenom = 8;
  } else {
    jpegScaleDenom = chooseJpegScale(targetScale, jpegScaleOption);
  }

  if (destWidth <= 0 || destHeight <= 0) {
    LOG_ERR("JPG", "Degenerate output dimensions %dx%d for %s, skipping render", destWidth, destHeight,
            imagePath.c_str());
    return false;
  }

  const int scaledSrcWidth = (srcWidth + jpegScaleDenom - 1) / jpegScaleDenom;
  const int scaledSrcHeight = (srcHeight + jpegScaleDenom - 1) / jpegScaleDenom;
  ctx.cropLeft = scaledCrop(cropLeft, jpegScaleDenom, scaledSrcWidth);
  ctx.cropTop = scaledCrop(cropTop, jpegScaleDenom, scaledSrcHeight);
  ctx.visibleWidth = scaledSrcWidth - 2 * ctx.cropLeft;
  ctx.visibleHeight = scaledSrcHeight - 2 * ctx.cropTop;
  ctx.dstWidth = destWidth;
  ctx.dstHeight = destHeight;
  ctx.fineScaleFPX = (int32_t)((int64_t)destWidth * FP_ONE / ctx.visibleWidth);
  ctx.invScaleFPX = (int32_t)((int64_t)ctx.visibleWidth * FP_ONE / destWidth);
  ctx.fineScaleFPY = (int32_t)((int64_t)destHeight * FP_ONE / ctx.visibleHeight);
  ctx.invScaleFPY = (int32_t)((int64_t)ctx.visibleHeight * FP_ONE / destHeight);

  LOG_DBG("JPG", "JPEG %dx%d (visible %dx%d) -> %dx%d (scale %.2f, jpegScale 1/%d, fineScale %.2f)%s", srcWidth,
          srcHeight, visibleWidth, visibleHeight, destWidth, destHeight, targetScale, jpegScaleDenom,
          (float)destWidth / ctx.visibleWidth, isProgressive ? " [progressive]" : "");

  // Photo path (RenderConfig::fullGrayLevels / autoContrast). Auto-contrast
  // needs the histogram before any pixel is written: stage the on-screen output
  // in PSRAM and write it after the decode, or failing that run a pre-pass.
  const bool level16 = config.fullGrayLevels && renderer.getGray4Target() != nullptr;
  std::unique_ptr<image_tone::ToneMap> toneMap;
  image_tone::GrayStage stage;
  image_tone::TonedSink toned;
  bool prepass = false;
  if (config.autoContrast) {
    // One ~1.3 KB block per decode (histogram + LUT), kept off the task stack.
    toneMap = makeUniqueNoThrow<image_tone::ToneMap>();
    if (!toneMap) {
      LOG_ERR("JPG", "OOM: tone map, drawing without auto-contrast");
    } else {
      // The on-screen part of the output, as the draw callback clamps it.
      const int stageX0 = std::max(0, -config.x);
      const int stageY0 = std::max(0, -config.y);
      const int stageX1 = std::min(destWidth, ctx.screenWidth - config.x);
      const int stageY1 = std::min(destHeight, ctx.screenHeight - config.y);
      if (stage.begin(stageX0, stageY0, stageX1 - stageX0, stageY1 - stageY0)) {
        toned.stage = &stage;
        toned.histogram = toneMap->histogram;
      } else {
        prepass = true;
      }
    }
  }
  if (level16 || toneMap) {
    toned.writer.lut = toneMap ? toneMap->lut : nullptr;  // filled before any pixel goes through it
    toned.writer.level16 = level16;
    toned.writer.dither = config.useDithering;
    ctx.toned = &toned;
  }
  if (prepass) {
    rc = histogramPrepassThenReopen(*jpeg, imagePath, srcWidth, srcHeight, cropLeft, cropTop, *toneMap);
    if (rc != 1) {
      LOG_ERR("JPG", "Failed to reopen JPEG after the auto-contrast pre-pass (err=%d)", jpeg->getLastError());
      return false;
    }
    toneMap->buildCurve();
  }

  // Set pixel type to 8-bit grayscale (must be after open())
  jpeg->setPixelType(EIGHT_BIT_GRAYSCALE);
  jpeg->setUserPointer(&ctx);

  // Start streaming the pixel cache to disk. The band only needs to hold the
  // tallest single decode block: a JPEGDEC MCU cell is at most 16 scaled-source
  // rows tall, which our fine scale maps to this many output rows. The photo
  // path writes no cache (see RenderConfig).
  ctx.caching = !config.cachePath.empty() && !config.autoContrast && !level16;
  if (ctx.caching) {
    const int maxBlockDstRows = (int)(((int64_t)16 * ctx.fineScaleFPY) >> FP_SHIFT) + 2;
    if (!ctx.cache.begin(config.cachePath, destWidth, destHeight, config.x, config.y, maxBlockDstRows)) {
      LOG_ERR("JPG", "Failed to start cache stream, continuing without caching");
      ctx.caching = false;
    }
  }

  unsigned long decodeStart = millis();
  ctx.lastYieldMs = decodeStart;
  rc = jpeg->decode(0, 0, jpegScaleOption);
  unsigned long decodeTime = millis() - decodeStart;

  if (rc != 1) {
    LOG_ERR("JPG", "Decode failed (rc=%d, lastError=%d)", rc, jpeg->getLastError());
    if (ctx.caching) ctx.cache.abort();
    return false;
  }

  LOG_DBG("JPG", "JPEG decoding complete - render time: %lu ms", decodeTime);

  if (stage.active()) {
    const unsigned long writeStart = millis();
    toneMap->buildCurve();
    stage.write(renderer, toned.writer, config.x, config.y);
    LOG_DBG("JPG", "Auto-contrast staged write: %lu ms", millis() - writeStart);
  }

  // Finalize the streamed cache file. Note: a flush failure mid-decode clears
  // ctx.caching (the partial file is dropped), so re-read the flag here.
  if (ctx.caching) {
    ctx.cache.finalize();
  }

  return true;
}

bool JpegToFramebufferConverter::supportsFormat(const std::string& extension) {
  return FsHelpers::hasJpgExtension(extension);
}
