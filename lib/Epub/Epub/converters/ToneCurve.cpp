#include "ToneCurve.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <Logging.h>
#include <esp_heap_caps.h>

#include <cmath>
#include <cstring>

#include "ImageToFramebufferDecoder.h"

namespace image_tone {
namespace {

constexpr uint32_t CLIP_DIVISOR = 100;  // 1% of the pixels clipped at each end
constexpr float MAX_GAIN = 2.5f;        // a near-flat photo is not stretched further
constexpr float GAMMA = 1.2f;           // > 1 darkens mid greys, which read light on e-ink
// Left free for everyone else (the gray4 frame is already held), as PixelCache.
constexpr size_t STAGE_PSRAM_RESERVE = 1024 * 1024;
constexpr int STAGE_BAND_ROWS = 32;

}  // namespace

void ToneMap::buildCurve() {
  uint32_t total = 0;
  for (const uint32_t count : histogram) total += count;

  int lo = 0;
  int hi = 255;
  if (total > 0) {
    const uint32_t clip = total / CLIP_DIVISOR;
    uint32_t below = 0;
    while (lo < 255 && (below += histogram[lo]) <= clip) lo++;
    uint32_t above = 0;
    while (hi > 0 && (above += histogram[hi]) <= clip) hi--;
    if (hi < lo) hi = lo;
  }

  // Gain cap: a window narrower than 255 / MAX_GAIN widens to that span, the
  // slack split in proportion to the room on each side, so the photo keeps its
  // place in the range (a flat light image stays light) and the curve moves
  // smoothly as a photo's spread crosses the cap.
  float low = static_cast<float>(lo);
  float high = static_cast<float>(hi);
  const float minSpan = 255.0f / MAX_GAIN;
  if (high - low < minSpan) {
    const float slack = minSpan - (high - low);
    const float room = low + (255.0f - high);  // > 0: the span is under 255
    low -= slack * low / room;
    high = low + minSpan;
  }

  const float scale = 1.0f / (high - low);
  for (int v = 0; v < 256; v++) {
    float t = (static_cast<float>(v) - low) * scale;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    lut[v] = static_cast<uint8_t>(powf(t, GAMMA) * 255.0f + 0.5f);
  }
  LOG_DBG("TONE", "Auto-contrast: 1-99%% %d..%d, window %.1f..%.1f, %u px", lo, hi, low, high,
          static_cast<unsigned>(total));
}

GrayStage::~GrayStage() {
  if (data) heap_caps_free(data);
}

bool GrayStage::begin(const int x0, const int y0, const int w, const int h) {
  if (data || w <= 0 || h <= 0) return false;
  const size_t bytes = static_cast<size_t>(w) * static_cast<size_t>(h);
  if (heap_caps_get_free_size(MALLOC_CAP_SPIRAM) < bytes + STAGE_PSRAM_RESERVE ||
      heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM) < bytes) {
    return false;
  }
  // PSRAM specifically (a full-screen stage is ~2.6 MB), hence heap_caps
  // rather than makeUniqueNoThrow; the destructor frees it.
  data = static_cast<uint8_t*>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!data) return false;
  memset(data, 0xFF, bytes);  // white (never drawn) until decoded
  originX = x0;
  originY = y0;
  width = w;
  height = h;
  return true;
}

void GrayStage::write(GfxRenderer& renderer, const TonedWriter& out, const int screenX, const int screenY) const {
  if (!data) return;
  DirectPixelWriter pw;
  pw.init(renderer);
  const int left = screenX + originX;
  const int top = screenY + originY;
  uint32_t lastYieldMs = millis();

  // 16-level target in a portrait orientation: an image row is a physical
  // column, so walk bands of rows column by column. A band's pixels of one
  // column then share one 4bpp row (one cache line) and the band's source rows
  // stay cached: ImageBlock's blocked walk, for the same reason.
  if (pw.fb4 && pw.phyXStepY != 0) {
    for (int row0 = 0; row0 < height; row0 += STAGE_BAND_ROWS) {
      ImageToFramebufferDecoder::yieldDuringDecode(lastYieldMs);
      const int row1 = (height - row0 < STAGE_BAND_ROWS) ? height : row0 + STAGE_BAND_ROWS;
      const uint8_t* band = data + static_cast<size_t>(row0) * width;
      for (int col = 0; col < width; col++) {
        const uint8_t* src = band + col;
        for (int row = row0; row < row1; row++, src += width) {
          out.writeAt(pw, left + col, top + row, *src);
        }
      }
    }
    return;
  }

  for (int row = 0; row < height; row++) {
    ImageToFramebufferDecoder::yieldDuringDecode(lastYieldMs);
    const int outY = top + row;
    pw.beginRow(outY);
    const uint8_t* src = data + static_cast<size_t>(row) * width;
    for (int col = 0; col < width; col++) {
      out.write(pw, left + col, outY, src[col]);
    }
  }
}

}  // namespace image_tone
