#pragma once

#include <string>

#include "CarouselImages.h"
#include "TimedDashboardActivity.h"

// The Image tile opened full screen: the carousel's picture (BMP, PNG, JPEG)
// in grey, changing on the carousel interval. Down and Up step forward and back and restart the
// interval; Back returns to the tiles.
class CarouselDashboardActivity final : public TimedDashboardActivity {
 public:
  explicit CarouselDashboardActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : TimedDashboardActivity("CarouselDashboard", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;

 private:
  unsigned long nextUpdateMs() const override { return images.nextChangeMs(); }
  void update() override;
  bool handleKeys() override;
  void draw(RenderLock&& lock) override;

  void drawMessage(const char* message) const;

  CarouselImages images;
};
