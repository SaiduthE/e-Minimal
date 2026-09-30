#pragma once

#include <cstdint>

#include "DashboardConfigStore.h"
#include "activities/UiListActivity.h"
#include "components/OptionPopup.h"
#include "components/media/cover-grid.h"

namespace dashboard_layouts {
// The layout in use's name, or "No layout" when every layout was deleted.
const char* activeName(const DashboardConfigStore& config);
}  // namespace dashboard_layouts

// Change Layout: every layout and a New Layout card as a grid of thumbnails,
// each layout's tiles drawn to scale in the shape the launched dashboard takes
// (SETTINGS.dashboardOrientation) with the widget named in each tile. Up/Down
// step through the cards. Select makes a layout the one in use and returns;
// holding Select opens the dashboard portal on its editor (where it can also
// be renamed or deleted). Select on the New Layout card opens the portal's
// editor for a new one.
//
// A UiListActivity like GamesActivity: the cards are the list rows in reading
// order, so selection and taps route through the row action.
class LayoutPickerActivity final : public UiListActivity {
 public:
  explicit LayoutPickerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void render(RenderLock&& lock) override;

 private:
  static constexpr uint8_t MAX_CARDS = DashboardConfigStore::MAX_LAYOUTS + 1;

  enum class Kind : uint8_t { Layout, New };
  // A copy of one layout, so the render task never reads the store.
  struct Card {
    Kind kind = Kind::Layout;
    uint8_t id = 0;  // the layout's id
    uint8_t tileCount = 0;
    DashboardTile tiles[CustomLayout::MAX_TILES]{};
    const char* title = nullptr;
    char name[CustomLayout::MAX_NAME_LENGTH + 1]{};
  };

  int listCount() const override { return cardCount_; }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onRowLongPress(int index) override;
  bool handleCustomInput() override;
  bool handleButtons() override;
  void navigateButtons() override;
  const char* headerTitle() const override;

  bool paintCard(freeink::ui::DrawTarget& target, freeink::ui::Rect rect, uint16_t index);
  void drawTiles(freeink::ui::DrawTarget& target, freeink::ui::Rect area, const Card& card) const;
  // Opens the portal where this card's layout is edited.
  void editCard(int index);

  Card cards_[MAX_CARDS]{};
  freeink::ui::CoverGridItem items_[MAX_CARDS]{};
  uint8_t cardCount_ = 0;
  uint8_t activeIndex_ = 0;
  bool portrait_ = false;

  // Set in buildScreen() for paintCard(), both on the render task.
  int16_t captionHeight_ = 0;
  freeink::ui::TextStyle tileText_{};
  freeink::ui::TextStyle captionText_{};
  // Component props stay off the render task's stack (as GamesActivity's).
  freeink::ui::CoverGridProps grid_;

  OptionPopup popup;
};
