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
class PowerMenuActivity final : public UiListActivity {
 public:
  enum class Action { SLEEP, GO_HOME, REFRESH_SCREEN };
  static constexpr int MENU_ITEM_COUNT = 3;
  static constexpr int APP_COUNT = 3;

  explicit PowerMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;

 private:
  // Ring index: [0, APP_COUNT) are the tiles, then the rows.
  static constexpr int RING_COUNT = APP_COUNT + MENU_ITEM_COUNT;

  // Rows only; the tiles are the ring's leading entries (syncListViewport's selectionOffset).
  int listCount() const override { return MENU_ITEM_COUNT; }
  void buildScreen(UiScreen& screen) override;
  // Row index, not ring index.
  void activateIndex(int index) override;
  void onRowAction(const freeink::ui::ActionEvent& event) override;
  bool handleButtons() override;
  void navigateButtons() override;
  const char* headerTitle() const override;

  void activateApp(AppId target);
  void drawAppTiles(UiScreen& screen, freeink::ui::Rect band);
  int16_t tileBandHeight(UiScreen& screen) const;

  // The app open underneath, read once on entry.
  AppId currentApp = AppId::READER;

  // Static rows, built once in the constructor (see NetworkModeSelectionActivity).
  freeink::ui::ListItem rowItems_[MENU_ITEM_COUNT]{};
};
