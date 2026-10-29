#pragma once
#include <GfxRenderer.h>

#include <cstddef>

#include "ScreenshotInfo.h"

class ScreenshotUtil {
 public:
  static void takeScreenshot(GfxRenderer& renderer);
  // flipped: the panel is mounted upside down (GfxRenderer::isMountFlipped).
  static bool saveFramebufferAsBmp(const char* filename, const uint8_t* framebuffer, int width, int height,
                                   bool flipped = false);

 private:
  static void buildFilename(const ScreenshotInfo& info, char* buf, size_t bufSize);
};
