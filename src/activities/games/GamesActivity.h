#pragma once

#include <PartyGames.h>

#include "activities/UiListActivity.h"
#include "components/media/cover-grid.h"

// The Games app's root screen: the party games as a 2x2 grid of picture tiles,
// the selected game's description under it. The radio stays off here; picking
// a game opens Game Night for it, which raises the hotspot. It is an app root
// like the Dashboard, so Back does nothing: the power menu switches apps.
//
// Still a UiListActivity: the tiles are the list rows in reading order, so
// Up/Down step the selection and taps route through the row action.
class GamesActivity final : public UiListActivity {
 public:
  explicit GamesActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  bool isHomeActivity() const override { return true; }

 private:
  static constexpr int SEATS_LENGTH = 24;
  static constexpr uint8_t GRID_COLUMNS = 2;
  static constexpr uint8_t GRID_ROWS = (party::GAME_COUNT + GRID_COLUMNS - 1) / GRID_COLUMNS;

  int listCount() const override { return party::GAME_COUNT; }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onBackButton() override {}
  const char* headerTitle() const override;
  void drawChrome() override;
  void drawFooter() override;
  bool paintTile(freeink::ui::DrawTarget& target, freeink::ui::Rect tile, uint16_t index);

  // Static tiles, built once in the constructor.
  freeink::ui::CoverGridItem tiles_[party::GAME_COUNT]{};
  char seats_[party::GAME_COUNT][SEATS_LENGTH]{};
  // Component props stay off the render task's stack (as EMinimalHomeUi's).
  freeink::ui::CoverGridProps grid_;
};
