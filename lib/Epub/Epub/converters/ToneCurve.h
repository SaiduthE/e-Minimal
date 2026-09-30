#pragma once

#include <stddef.h>
#include <stdint.h>

#include "DirectPixelWriter.h"
#include "DitherUtils.h"

class GfxRenderer;

// Photo tone handling for the JPEG and PNG converters: RenderConfig::autoContrast
// (a percentile stretch plus an e-ink gamma, applied through a 256-entry LUT)
// and RenderConfig::fullGrayLevels (16-level quantizing on the gray4 target).
// The default decode path uses none of this.
namespace image_tone {

// Grey histogram and the curve built from it. ~1.3 KB, so an auto-contrast
// decode allocates one instead of putting it on the decode task's stack.
struct ToneMap {
  uint32_t histogram[256];
  uint8_t lut[256];

  // Map the histogram's 1st..99th percentile to 0..255 with the gain capped
  // at 2.5x, then darken mid greys (gamma 1.2). An empty histogram gives the
  // gamma alone.
  void buildCurve();
};

// Toned pixel output: the curve (if any), then 16-level or 2bpp quantizing.
struct TonedWriter {
  const uint8_t* lut = nullptr;  // nullptr: no curve
  bool level16 = false;          // fullGrayLevels on an active gray4 target
  bool dither = true;

  // The 16-level nibble, or the 2bpp level (0 black .. 3 white).
  inline uint8_t quantize(uint8_t gray, const int outX, const int outY) const {
    if (lut) gray = lut[gray];
    if (level16) return dither ? applyBayerDither16Level(gray, outX, outY) : quantize16Level(gray);
    if (dither) return applyBayerDither4Level(gray, outX, outY);
    return static_cast<uint8_t>(gray / 85);
  }

  // Current row of `pw` (after beginRow).
  inline void write(const DirectPixelWriter& pw, const int outX, const int outY, const uint8_t gray,
                    const bool writeWhite = false) const {
    const uint8_t value = quantize(gray, outX, outY);
    if (level16) {
      pw.writeLevel16(outX, value, writeWhite);
    } else {
      pw.writePixel(outX, value, writeWhite);
    }
  }

  // Random access; the gray4 target only (pw.fb4 set).
  inline void writeAt(const DirectPixelWriter& pw, const int outX, const int outY, const uint8_t gray) const {
    const uint8_t value = quantize(gray, outX, outY);
    if (level16) {
      pw.writeLevel16At(outX, outY, value);
    } else {
      pw.writePixelAt(outX, outY, value);
    }
  }
};

// Single-pass auto-contrast: the decode parks the on-screen part of its output
// as 8-bit grey in PSRAM and counts it; once the histogram is whole, write()
// places it through the curve. Spares the second read of the file that a
// histogram pre-pass costs.
class GrayStage {
 public:
  GrayStage() = default;
  GrayStage(const GrayStage&) = delete;
  GrayStage& operator=(const GrayStage&) = delete;
  ~GrayStage();

  // Stage decoder output pixels [x0, x0 + w) x [y0, y0 + h). False, holding
  // nothing, when PSRAM has no room (then use a pre-pass).
  bool begin(int x0, int y0, int w, int h);
  bool active() const { return data != nullptr; }

  // Pixels outside the staged rectangle are dropped.
  inline void put(const int dstX, const int dstY, const uint8_t gray, uint32_t* histogram) const {
    const unsigned sx = static_cast<unsigned>(dstX - originX);
    const unsigned sy = static_cast<unsigned>(dstY - originY);
    if (sx >= static_cast<unsigned>(width) || sy >= static_cast<unsigned>(height)) return;
    data[static_cast<size_t>(sy) * static_cast<size_t>(width) + sx] = gray;
    histogram[gray]++;
  }

  // Draw the staged pixels through `out`; decoder output (0, 0) lands at
  // screen (screenX, screenY), i.e. RenderConfig x/y.
  void write(GfxRenderer& renderer, const TonedWriter& out, int screenX, int screenY) const;

 private:
  uint8_t* data = nullptr;
  int originX = 0;
  int originY = 0;
  int width = 0;
  int height = 0;
};

// Where a toned decode sends each grey pixel: the stage, or the frame now.
struct TonedSink {
  TonedWriter writer;
  GrayStage* stage = nullptr;     // single-pass auto-contrast
  uint32_t* histogram = nullptr;  // with stage

  inline void put(const DirectPixelWriter& pw, const int dstX, const int dstY, const int outX, const int outY,
                  const uint8_t gray, const bool writeWhite = false) const {
    if (stage) {
      stage->put(dstX, dstY, gray, histogram);
      return;
    }
    writer.write(pw, outX, outY, gray, writeWhite);
  }
};

}  // namespace image_tone
