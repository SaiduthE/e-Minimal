#include "DashboardImage.h"

#include <Bitmap.h>
#include <Epub/converters/JpegToFramebufferConverter.h>
#include <Epub/converters/PngToFramebufferConverter.h>
#include <FsHelpers.h>
#include <HalDisplay.h>
#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <cstring>
#include <functional>

namespace dashboard_image {
namespace {

// Where a width x height picture lands in area: the source crop (total
// fractions, half from each side, as RenderConfig and drawBitmap take them)
// and the drawn box, centred.
struct Placement {
  float cropX = 0.0f;
  float cropY = 0.0f;
  Rect box;
};

// canEnlarge: the drawer scales up as well as down (the JPEG and PNG
// converters with useExactDimensions; drawBitmap only shrinks).
Placement place(const Rect& area, const int width, const int height, const Fit fit, const bool canEnlarge) {
  Placement out;
  float visibleWidth = static_cast<float>(width);
  float visibleHeight = static_cast<float>(height);
  if (fit == Fit::Fill) {
    const float sourceAspect = visibleWidth / visibleHeight;
    const float areaAspect = static_cast<float>(area.width) / static_cast<float>(area.height);
    if (sourceAspect > areaAspect) {
      out.cropX = 1.0f - areaAspect / sourceAspect;
      visibleWidth *= 1.0f - out.cropX;
    } else if (sourceAspect < areaAspect) {
      out.cropY = 1.0f - sourceAspect / areaAspect;
      visibleHeight *= 1.0f - out.cropY;
    }
    if (canEnlarge) {
      // Cropped to the area's shape, it covers the area exactly; no rounding
      // to leave a hairline at an edge.
      out.box = area;
      return out;
    }
  }
  const float scale = std::min(1.0f, std::min(static_cast<float>(area.width) / visibleWidth,
                                              static_cast<float>(area.height) / visibleHeight));
  const int drawnWidth = std::max(1, static_cast<int>(visibleWidth * scale));
  const int drawnHeight = std::max(1, static_cast<int>(visibleHeight * scale));
  out.box = Rect{area.x + (area.width - drawnWidth) / 2, area.y + (area.height - drawnHeight) / 2, drawnWidth,
                 drawnHeight};
  return out;
}

template <typename Converter>
bool drawDecoded(GfxRenderer& renderer, const Rect& area, const std::string& path, const Fit fit) {
  ImageDimensions dimensions;
  if (!Converter::getDimensionsStatic(path, dimensions) || dimensions.width <= 0 || dimensions.height <= 0) {
    return false;
  }
  const Placement placement = place(area, dimensions.width, dimensions.height, fit, true);
  RenderConfig config{placement.box.x, placement.box.y, placement.box.width, placement.box.height};
  config.sourceCropX = placement.cropX;
  config.sourceCropY = placement.cropY;
  // The box is already the fitted size; a second fit in the decoder would lose
  // a pixel to rounding and leave a hairline gap in a filled tile. It is also
  // how the converters enlarge a small picture.
  config.useExactDimensions = true;
  // Photos, for the tile and the full-screen view alike: all 16 levels on the
  // grey target (no effect on the 1bpp frame) and the contrast stretched.
  config.fullGrayLevels = true;
  config.autoContrast = true;
  Converter converter;
  return converter.decodeToFramebuffer(path, renderer, config);
}

bool drawBmp(GfxRenderer& renderer, const Rect& area, const std::string& path, const Fit fit) {
  HalFile file;
  if (!Storage.openFileForRead("DASHIMG", path, file)) return false;
  Bitmap bitmap(file, true, false);
  if (bitmap.parseHeaders() != BmpReaderError::Ok) return false;
  const Placement placement = place(area, bitmap.getWidth(), bitmap.getHeight(), fit, false);
  // drawBitmap fits the cropped source into the max size itself.
  return renderer.drawBitmap(bitmap, placement.box.x, placement.box.y, placement.box.width, placement.box.height,
                             placement.cropX, placement.cropY);
}

// The frame the renderer draws into now: the 16-level target while one is
// active (4 bits a pixel, left pixel in the high nibble), else the 1bpp frame
// (MSB first). Both are rows of physical panel pixels.
struct Frame {
  uint8_t* data = nullptr;
  uint32_t stride = 0;
  int bitsPerPixel = 1;
};

Frame currentFrame(const GfxRenderer& renderer) {
  if (uint8_t* gray4 = renderer.getGray4Target()) return Frame{gray4, renderer.getGray4Stride(), 4};
  return Frame{renderer.getFrameBuffer(), renderer.getDisplayWidthBytes(), 1};
}

// A logical rect's pixels in panel memory: its physical box (inclusive) and
// the bytes each of its rows spans there, with masks for the rect's pixels in
// the first and last byte.
struct Span {
  int y0 = 0;
  int rows = 0;
  uint32_t byteX0 = 0;
  uint32_t rowBytes = 0;
  uint8_t headMask = 0xFF;
  uint8_t tailMask = 0xFF;
  size_t bytes() const { return static_cast<size_t>(rowBytes) * static_cast<size_t>(rows); }
};

// GfxRenderer's rotateCoordinates: logical (x, y) to the panel pixel it lands on.
void toPanel(const GfxRenderer& renderer, const int x, const int y, int& panelX, int& panelY) {
  const int panelWidth = renderer.getDisplayWidth();
  const int panelHeight = renderer.getDisplayHeight();
  switch (renderer.getOrientation()) {
    case GfxRenderer::Portrait:
      panelX = y;
      panelY = panelHeight - 1 - x;
      break;
    case GfxRenderer::LandscapeClockwise:
      panelX = panelWidth - 1 - x;
      panelY = panelHeight - 1 - y;
      break;
    case GfxRenderer::PortraitInverted:
      panelX = panelWidth - 1 - y;
      panelY = x;
      break;
    case GfxRenderer::LandscapeCounterClockwise:
    default:
      panelX = x;
      panelY = y;
      break;
  }
}

bool spanOf(const GfxRenderer& renderer, const Rect& rect, const int bitsPerPixel, Span& out) {
  // Clipped to the screen, so every row and byte lies inside the frame.
  const int x0 = std::max(0, rect.x);
  const int y0 = std::max(0, rect.y);
  const int x1 = std::min(renderer.getScreenWidth(), rect.x + rect.width) - 1;
  const int y1 = std::min(renderer.getScreenHeight(), rect.y + rect.height) - 1;
  if (x0 > x1 || y0 > y1) return false;
  // Rotation is rigid, so two opposite corners bound the rect on the panel.
  int ax, ay, bx, by;
  toPanel(renderer, x0, y0, ax, ay);
  toPanel(renderer, x1, y1, bx, by);
  const int panelX0 = std::min(ax, bx);
  const int panelX1 = std::max(ax, bx);
  const int perByte = 8 / bitsPerPixel;
  out.y0 = std::min(ay, by);
  out.rows = std::max(ay, by) - out.y0 + 1;
  out.byteX0 = static_cast<uint32_t>(panelX0 / perByte);
  out.rowBytes = static_cast<uint32_t>(panelX1 / perByte) - out.byteX0 + 1;
  out.headMask = static_cast<uint8_t>(0xFFu >> ((panelX0 % perByte) * bitsPerPixel));
  out.tailMask = static_cast<uint8_t>(0xFFu << ((perByte - 1 - panelX1 % perByte) * bitsPerPixel));
  return true;
}

inline void merge(uint8_t& dst, const uint8_t src, const uint8_t mask) {
  dst = static_cast<uint8_t>((dst & ~mask) | (src & mask));
}

}  // namespace

bool isImage(const std::string_view name) {
  if (name.empty() || name.front() == '.') return false;
  return FsHelpers::hasBmpExtension(name) || FsHelpers::hasPngExtension(name) || FsHelpers::hasJpgExtension(name);
}

bool draw(GfxRenderer& renderer, const Rect& area, const std::string& path, const Fit fit) {
  const std::string_view name{path};
  bool drawn = false;
  if (FsHelpers::hasJpgExtension(name)) {
    drawn = drawDecoded<JpegToFramebufferConverter>(renderer, area, path, fit);
  } else if (FsHelpers::hasPngExtension(name)) {
    drawn = drawDecoded<PngToFramebufferConverter>(renderer, area, path, fit);
  } else if (FsHelpers::hasBmpExtension(name)) {
    drawn = drawBmp(renderer, area, path, fit);
  }
  if (!drawn) LOG_ERR("DASHIMG", "Could not draw %s", path.c_str());
  return drawn;
}

bool drawFullScreen(GfxRenderer& renderer, const std::string& path) {
  const Rect screen{0, 0, renderer.getScreenWidth(), renderer.getScreenHeight()};
  renderer.clearScreen();
  // The reader's grey page path (EpubReaderActivity): one decode straight into
  // the 16-level frame, loaded whole; the driver hands the 1bpp base back.
  if (renderer.supportsGray4() && renderer.beginGray4Target(false)) {
    const bool drawn = draw(renderer, screen, path);
    renderer.endGray4Target();
    if (drawn) {
      const bool shown = renderer.displayGray4Buffer(renderer.getGray4Buffer(), HalDisplay::HALF_REFRESH,
                                                     /*updateFrameBuffer=*/true);
      if (!shown) renderer.gray4ToFrameBuffer();
      renderer.releaseGray4Target();
      if (!shown) {
        LOG_ERR("DASHIMG", "gray4 display refused; showing %s black and white", path.c_str());
        renderer.displayBuffer(HalDisplay::HALF_REFRESH);
      }
      return true;
    }
    renderer.releaseGray4Target();
    renderer.clearScreen();
    return false;
  }
  if (!draw(renderer, screen, path)) return false;
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
  return true;
}

void maskCorners(const GfxRenderer& renderer, const Rect& rect, const int radius) {
  // drawRoundedRect's radius and arc: the outline's outermost pixel on the row
  // dy above (below) a corner's centre is `inside` px out from the centre,
  // the widest x with x^2 + dy^2 <= r^2; the run beyond it is outside.
  const int r = std::min({radius, rect.width / 2, rect.height / 2});
  if (r <= 0) return;
  const int r2 = r * r;
  const int right = rect.x + rect.width;
  const int bottom = rect.y + rect.height - 1;
  int inside = r;
  for (int dy = 1; dy <= r; dy++) {
    while (inside > 0 && inside * inside + dy * dy > r2) inside--;
    const int run = r - inside;
    if (run <= 0) continue;
    const int top = rect.y + r - dy;
    const int low = bottom - r + dy;
    renderer.fillRect(rect.x, top, run, 1, false);
    renderer.fillRect(right - run, top, run, 1, false);
    renderer.fillRect(rect.x, low, run, 1, false);
    renderer.fillRect(right - run, low, run, 1, false);
  }
}

TileCache::Key TileCache::keyFor(const GfxRenderer& renderer, const Rect& rect, const std::string& path) const {
  Key k;
  k.pathHash = std::hash<std::string>{}(path);
  k.rect = rect;
  k.orientation = static_cast<int>(renderer.getOrientation());
  k.gray4 = renderer.getGray4Target() != nullptr;
  return k;
}

bool TileCache::matches(const GfxRenderer& renderer, const Rect& rect, const std::string& path) const {
  if (!valid || !pixels) return false;
  const Key k = keyFor(renderer, rect, path);
  return k.pathHash == key.pathHash && k.orientation == key.orientation && k.gray4 == key.gray4 &&
         k.rect.x == key.rect.x && k.rect.y == key.rect.y && k.rect.width == key.rect.width &&
         k.rect.height == key.rect.height;
}

bool TileCache::restore(const GfxRenderer& renderer, const Rect& rect, const std::string& path) const {
  if (!matches(renderer, rect, path)) return false;
  const Frame frame = currentFrame(renderer);
  Span span;
  if (!frame.data || !spanOf(renderer, rect, frame.bitsPerPixel, span) || span.bytes() > capacity) return false;
  const uint8_t* src = pixels.get();
  const uint32_t last = span.rowBytes - 1;
  for (int row = 0; row < span.rows; row++, src += span.rowBytes) {
    uint8_t* dst = frame.data + static_cast<uint32_t>(span.y0 + row) * frame.stride + span.byteX0;
    if (last == 0) {
      merge(dst[0], src[0], static_cast<uint8_t>(span.headMask & span.tailMask));
      continue;
    }
    merge(dst[0], src[0], span.headMask);
    if (last > 1) memcpy(dst + 1, src + 1, last - 1);
    merge(dst[last], src[last], span.tailMask);
  }
  return true;
}

bool TileCache::capture(const GfxRenderer& renderer, const Rect& rect, const std::string& path) {
  valid = false;
  const Frame frame = currentFrame(renderer);
  Span span;
  if (!frame.data || !spanOf(renderer, rect, frame.bitsPerPixel, span)) return false;
  const size_t bytes = span.bytes();
  if (bytes > capacity) {
    // Free the old block first: the new one may fit where it was.
    pixels.reset();
    capacity = 0;
    pixels = HalMemory::allocatePsram(bytes);
    if (!pixels) {
      if (bytes != failedBytes) {
        LOG_ERR("DASHIMG", "No %u B PSRAM for the tile cache; the tile decodes on every paint",
                static_cast<unsigned>(bytes));
      }
      failedBytes = bytes;
      return false;
    }
    capacity = bytes;
    failedBytes = 0;
  }
  uint8_t* dst = pixels.get();
  for (int row = 0; row < span.rows; row++, dst += span.rowBytes) {
    memcpy(dst, frame.data + static_cast<uint32_t>(span.y0 + row) * frame.stride + span.byteX0, span.rowBytes);
  }
  key = keyFor(renderer, rect, path);
  valid = true;
  return true;
}

void TileCache::release() {
  pixels.reset();
  capacity = 0;
  failedBytes = 0;
  valid = false;
}

}  // namespace dashboard_image
