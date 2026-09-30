#pragma once

#include "CrossPointState.h"
#include "activities/UiListActivity.h"

// The power button's long-press menu (eMinimal 7.8). ActivityManager::openPowerMenu()
// pushes it over whatever is on screen. Three app tiles (Reader, Dashboard,
// Games) sit above the rows; Up/Down walk one ring through the tiles and then
// the rows. A tile switches to that app, or just closes the menu when that app
// is already open. Rows that leave the current screen (Sleep, Home) act from
// here; rows that act on the screen underneath (Refresh) return a MenuResult
// for openPowerMenu()'s handler to apply after the pop, before that screen
// repaints. Back closes it.
//
// Mode::LAUNCHER is the same page as a root screen: boot and wake show it
// instead of reopening the Dashboard or Games. Nothing is open underneath, so
// every tile switches, Sleep is the only row, Back does nothing, and the
// cursor starts on the app last used. It keeps the "PowerMenu" name so a
// Power hold does not stack the menu on top of it.
class PowerMenuActivity final : public UiListActivity {
 public:
  enum class Action { SLEEP, GO_HOME, REFRESH_SCREEN };
  enum class Mode : uint8_t { POWER_MENU, LAUNCHER };
  static constexpr int MENU_ITEM_COUNT = 3;
  static constexpr int APP_COUNT = 3;

  explicit PowerMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, Mode mode = Mode::POWER_MENU,
                             bool cleanInitialRefresh = false);

  void onEnter() override;
  bool isHomeActivity() const override { return mode == Mode::LAUNCHER; }

 private:
  // Sleep only: the launcher has no screen underneath to go home from or refresh.
  static constexpr int LAUNCHER_ROW_COUNT = 1;

  // Rows only; the tiles are the ring's leading entries (syncListViewport's selectionOffset).
  int listCount() const override { return mode == Mode::LAUNCHER ? LAUNCHER_ROW_COUNT : MENU_ITEM_COUNT; }
  // Ring index: [0, APP_COUNT) are the tiles, then the rows.
  int ringCount() const { return APP_COUNT + listCount(); }
  void buildScreen(UiScreen& screen) override;
  // Row index, not ring index.
  void activateIndex(int index) override;
  void onRowAction(const freeink::ui::ActionEvent& event) override;
  bool handleButtons() override;
  void navigateButtons() override;
  void onBackButton() override;
  const char* headerTitle() const override;
  void drawFooter() override;

  void activateApp(AppId target);
  void drawAppTiles(UiScreen& screen, freeink::ui::Rect band);
  int16_t tileBandHeight(UiScreen& screen) const;

  const Mode mode;
  // After a wake the panel still shows the sleep image: the first paint clears it.
  const bool cleanInitialRefresh;
  // The app open underneath (POWER_MENU) or last used (LAUNCHER), read once on entry.
  AppId currentApp = AppId::READER;

  // Static rows, built once in the constructor (see NetworkModeSelectionActivity).
  freeink::ui::ListItem rowItems_[MENU_ITEM_COUNT]{};
};
