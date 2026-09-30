#pragma once

#include "TimedDashboardActivity.h"
#include "WeatherService.h"

// The Weather tile opened full screen: now in detail and the next four days.
// Select fetches now; Back returns to the tiles. The restart the radio calls
// for happens when the Dashboard app is left, not here.
class WeatherDashboardActivity final : public TimedDashboardActivity {
 public:
  explicit WeatherDashboardActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : TimedDashboardActivity("WeatherDashboard", renderer, mappedInput) {}

  void onEnter() override;

 private:
  unsigned long nextUpdateMs() const override { return weather.nextFetchMs(); }
  void update() override;
  bool handleKeys() override;
  void draw(RenderLock&& lock) override;

  // Paints "Updating" first when asked to or when there is nothing to show yet.
  void fetch(bool announce);
  void drawMessage(const char* message, int top, int bottom) const;
  void drawForecast(int x, int top, int width, int bottom) const;
  void drawDays(int x, int top, int width, int bottom) const;
  void drawCentered(int fontId, int centerX, int y, const char* text, bool bold) const;

  WeatherService weather;
};
