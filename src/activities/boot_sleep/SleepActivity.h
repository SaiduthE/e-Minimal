#pragma once
#include <string>

#include "activities/Activity.h"

class Bitmap;
class HalFile;

class SleepActivity final : public Activity {
 public:
  // keepScreen: draw nothing, so the screen being left stays on the glass.
  // redraw: the sleep screen again, mid-sleep (the charging badge leaving): no popup first.
  explicit SleepActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, bool fromTimeout = false,
                         bool keepScreen = false, bool redraw = false)
      : Activity("Sleep", renderer, mappedInput), fromTimeout(fromTimeout), keepScreen(keepScreen), redraw(redraw) {}
  void onEnter() override;

 private:
  void renderDefaultSleepScreen() const;
  void renderCustomSleepScreen() const;
  void renderCoverSleepScreen() const;
  void renderBitmapSleepScreen(const Bitmap& bitmap, bool preserveBackground = false) const;
  bool renderSleepOverlayFile(HalFile& file, const char* pathForLog) const;
  bool renderTransparentOverlayPng(const std::string& path) const;
  bool renderSleepOverlayPath(const std::string& path) const;
  void renderLastScreenSleepScreen() const;
  void renderTransparentCustomSleepScreen() const;
  void renderBlankSleepScreen() const;

  bool fromTimeout = false;
  bool keepScreen = false;
  bool redraw = false;
};
