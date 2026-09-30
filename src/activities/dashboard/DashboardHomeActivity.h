#pragma once

#include <memory>

#include "activities/UiListActivity.h"

// The Dashboard app's root, portrait like the other apps: launch the tiled
// dashboard, change its layout, see which widgets are set up and choose which
// way the launched dashboard turns. Everything else is set in the web
// interface; the pickers open the dashboard portal for it. Back does nothing:
// the power menu switches apps. Leaving the app after the radio was used
// restarts on the way, like the other WiFi screens.
class DashboardHomeActivity final : public UiListActivity {
 public:
  explicit DashboardHomeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiListActivity("DashboardHome", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  bool isHomeActivity() const override { return true; }

 private:
  enum Row : int { LAUNCH, LAYOUT, WIDGETS, ORIENTATION, ROW_COUNT };
  static constexpr int VALUE_LENGTH = 64;

  int listCount() const override { return ROW_COUNT; }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onBackButton() override {}
  const char* headerTitle() const override;
  void drawFooter() override;

  // Reads the widget status, then fills the rows under the render lock.
  void refreshRows();
  // Pushes a screen and refreshes the rows when it returns. A turned screen
  // (the launched dashboard) comes back to portrait with a clean refresh.
  void open(std::unique_ptr<Activity> screen, bool turned = false);

  freeink::ui::ListItem rowItems_[ROW_COUNT]{};
  char values_[ROW_COUNT][VALUE_LENGTH]{};
};
