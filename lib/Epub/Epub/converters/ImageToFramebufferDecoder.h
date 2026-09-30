#pragma once
#include <HalStorage.h>

#include <cstdint>
#include <memory>
#include <string>

class GfxRenderer;

struct ImageDimensions {
  int16_t width;
  int16_t height;
};

struct RenderConfig {
  int x, y;
  int maxWidth, maxHeight;
  bool useGrayscale = true;
  bool useDithering = true;
  bool performanceMode = false;
  bool useExactDimensions = false;  // If true, use maxWidth/maxHeight as exact output size (no recalculation)
  float sourceCropX = 0.0f;         // Fraction cropped equally from the left and right edges
  float sourceCropY = 0.0f;         // Fraction cropped equally from the top and bottom edges
  bool preserveAlpha = false;       // Skip transparent pixels instead of compositing them against white
  std::string cachePath;            // If non-empty, decoder will write pixel cache to this path

  // Photo quality, JPEG and PNG (defaults render exactly as before). While
  // either is in effect no pixel cache is written: cachePath is ignored, since
  // the 2bpp .pxc holds neither 16 levels nor a curve a later default render
  // of the same path expects.
  bool fullGrayLevels = false;  // On a 16-level target (beginGray4Target): all 16 levels. Else no effect.
  bool autoContrast = false;    // Stretch the 1st..99th percentile to full range, then darken mid greys
};

class ImageToFramebufferDecoder {
 public:
  virtual ~ImageToFramebufferDecoder() = default;

  virtual bool decodeToFramebuffer(const std::string& imagePath, GfxRenderer& renderer, const RenderConfig& config) = 0;

  virtual bool getDimensions(const std::string& imagePath, ImageDimensions& dims) const = 0;

  virtual const char* getFormatName() const = 0;

  // Call from per-row/per-MCU decode callbacks (free functions, hence public):
  // yields one tick at most every 250 ms so multi-second decodes keep the idle
  // task (and its watchdog) fed. `lastYieldMs` is caller-held state,
  // initialized to the decode start time.
  static void yieldDuringDecode(uint32_t& lastYieldMs);

  // Validate decoder/header dimensions before narrowing them into the layout
  // representation. Shared by header probing and decoder fallbacks.
  static bool validateAndStoreDimensions(int64_t width, int64_t height, ImageDimensions& out, const char* format);

 protected:
  // Size validation helpers. The cap bounds decode TIME, not memory: both decoders
  // stream (JPEG in MCU bands at 1/2..1/8 coarse scale, PNG scanline-by-scanline
  // with its own width-based row-buffer guard), so RAM never scales with source
  // area. Without PSRAM (ESP32-C3) 8 MP admits real-world ebook covers (KDP
  // recommends 1600x2560 and 2000x3000) while keeping a worst-case single decode
  // in single-digit seconds. PSRAM builds run on the faster ESP32-S3 and show
  // phone photos (12 MP 4032x3024, 24 MP 5712x4284, 50 MP 8160x6120), so they
  // accept up to 50 MP and the longer decode that comes with it. The row
  // callbacks yield periodically so a long decode cannot starve the idle task's
  // watchdog.
  static constexpr int64_t MAX_SOURCE_DIMENSION = INT16_MAX;
#ifdef BOARD_HAS_PSRAM
  static constexpr int64_t MAX_SOURCE_PIXELS = 52428800;  // 50 MP (e.g. 8160 * 6120)
#else
  static constexpr int64_t MAX_SOURCE_PIXELS = 8388608;  // 8 MP (e.g. 2048 * 4096)
#endif

  void warnUnsupportedFeature(const std::string& feature, const std::string& imagePath);
};
