#pragma once

#include <atomic>
#include <cstdint>

#include "CarouselImages.h"
#include "DashboardConfigStore.h"
#include "DashboardImage.h"
#include "DashboardTiles.h"
#include "TimedDashboardActivity.h"
#include "WeatherService.h"

// The launched dashboard: the active layout's widgets in their tiles (a preset
// with its widgets, or a custom layout from the web interface), turned the way
// the Dashboard Orientation setting says (landscape either way, or portrait;
// the rest of the UI is portrait again on exit). Up/Down move the focus frame
// between tiles and Select opens the framed one full screen (Weather, To-do,
// Image; Date has no view); Back returns to the Dashboard home. Each widget
// updates on its own schedule (weather daily, image on the carousel interval,
// date at midnight) and the device light-sleeps between, with the focus frame
// hidden until the next wake. With a picture showing, the frame goes out in 16
// grey levels where the panel has them (the IT8951); the Image tile is decoded
// once and blitted from PSRAM after, so a focus move reads nothing from SD.
class TiledDashboardActivity final : public TimedDashboardActivity {
 public:
  explicit TiledDashboardActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : TimedDashboardActivity("Dashboard", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;

 private:
  unsigned long nextUpdateMs() const override;
  void update() override;
  bool handleKeys() override;
  bool prepareStandby() override;
  void onWake(bool byKey) override;
  void draw(RenderLock&& lock) override;

  bool shows(WidgetId widget) const;
  // Weather is fetched for its tile, and for the date tile until the clock is set.
  bool wantsWeather() const;
  // Reads the layout and every placed widget's data; after entry and after a
  // full view returns.
  void loadWidgets();
  void moveFocus(int delta);
  void openFocused();
  unsigned long nextDateChangeMs() const;
  // A widget's content changed: repaint with the clean waveform.
  void requestContentRepaint(bool immediate = false);

  // What a paint of the tiles cost the Image tile, for the repaint log.
  struct PaintStats {
    dashboard_tiles::ImagePaint image = dashboard_tiles::ImagePaint::Note;
    unsigned long imageMs = 0;
  };
  // Clears the frame being drawn and paints every tile, frames on top.
  PaintStats paintTiles();
  // LOG_DBG of one repaint's cost: painting (the Image tile's decode or blit
  // within it) and the display call. frame: "gray4" or "1bpp".
  static void logRepaint(const char* frame, HalDisplay::RefreshMode mode, const PaintStats& paint,
                         unsigned long start, unsigned long shownAt);

  // A focus change repaints with the quick waveform; content changes get a
  // clean one. Set on the loop task, taken by the render task.
  std::atomic<bool> focusOnlyRepaint{false};
  // Bumped by every content change; a focus-only repaint that finds it moved
  // since the last paint takes the clean waveform after all.
  std::atomic<uint32_t> contentVersion{0};
  uint32_t paintedVersion = UINT32_MAX;  // render task only
  // The panel refused a 16-level frame: black and white for the rest of this visit.
  bool gray4Refused = false;
  // The focus frame shows while awake and hides for standby.
  std::atomic<bool> showFocus{true};

  // Portrait dashboard: the grid is GRID_SHORT columns by GRID_LONG rows.
  bool portrait = false;
  DashboardTile tiles[CustomLayout::MAX_TILES] = {};
  uint8_t tileCount = 0;
  int focused = 0;

  WeatherService weather;
  CarouselImages images;
  // Each Image tile's finished pixels (PSRAM), by tile slot: a layout may place
  // the widget more than once. Written and read by draw() on the render task;
  // invalidated under the render lock, freed in onExit().
  dashboard_image::TileCache imageCaches[CustomLayout::MAX_TILES];
  dashboard_tiles::TodoSnapshot todo;
  unsigned long dateChange = 0;
};
