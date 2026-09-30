#include "LayoutPickerActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstdio>

#include "CrossPointSettings.h"
#include "DashboardPortalActivity.h"
#include "MappedInputManager.h"
#include "WidgetsActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace fui = freeink::ui;

namespace {
using Section = DashboardPortalActivity::Section;

// Wide landscape thumbnails two to a row, narrow portrait ones three.
constexpr uint8_t LANDSCAPE_COLUMNS = 2;
constexpr uint8_t PORTRAIT_COLUMNS = 3;
constexpr int COLUMN_GAP = 40;
constexpr int ROW_GAP = 16;
constexpr int THUMB_PAD = 8;
constexpr int TILE_GAP = 2;
constexpr int TILE_TEXT_PAD = 4;
constexpr int CAPTION_PAD = 6;
constexpr unsigned long EDIT_HOLD_MS = 700;

// Hand-placed sizes are written at 1x; the panel's UI scale applies.
int16_t px(const int v) { return static_cast<int16_t>(UITheme::scaledPx(v)); }

// The Home grid's selection ring and page margin, as GamesActivity uses them.
int16_t selectionFrameGap() { return px(10); }
int16_t selectionFrameWidth() { return px(4); }
int16_t sideMargin() { return px(24); }

void openPortal(Activity& host, GfxRenderer& renderer, MappedInputManager& mappedInput, const Section section,
                const uint8_t layoutId = 0) {
  auto portal = makeUniqueNoThrow<DashboardPortalActivity>(renderer, mappedInput, section, layoutId);
  if (!portal) {
    LOG_ERR("DASH", "OOM: dashboard portal");
    return;
  }
  // Leaving the portal restarts into the Dashboard app, so no result comes back.
  host.startActivityForResult(std::move(portal), [](const ActivityResult&) {});
}
}  // namespace

namespace dashboard_layouts {

const char* activeName(const DashboardConfigStore& config) {
  if (const CustomLayout* layout = config.activeLayout()) return layout->name.c_str();
  return tr(STR_DASH_NO_LAYOUT_SHORT);
}

}  // namespace dashboard_layouts

LayoutPickerActivity::LayoutPickerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("DashboardLayouts", renderer, mappedInput, true) {
  grid_.items = items_;
  grid_.action = ACTION_ROW;
  // Physical buttons stay in loop(); a touch hold edits like a held Select.
  grid_.inputMask = fui::InputTouch | fui::InputLongPress;
  grid_.selectionIndicator = fui::CoverGridSelectionIndicator::CoverFrame;
  // Plain cells: the card paints itself and the ring marks the selection.
  grid_.cellStyles.explicitlySet = true;
  grid_.cellStyles.selected.border = fui::Paint::solid(fui::Color::Black);
  grid_.labelAlign = fui::TextAlign::Center;
  grid_.labelFollowsCover = true;
  grid_.scrollIndicatorWidth = px(3);
  grid_.scrollIndicatorGap = px(12);
  grid_.coverPainterUserData = this;
  grid_.coverPainter = [](fui::DrawTarget& target, fui::Rect rect, const fui::CoverGridItem&, uint16_t index,
                          void* user) {
    return static_cast<LayoutPickerActivity*>(user)->paintCard(target, rect, index);
  };
}

void LayoutPickerActivity::onEnter() {
  const auto& config = DASHBOARD_CONFIG;
  const bool portrait = SETTINGS.dashboardOrientation == CrossPointSettings::DASHBOARD_PORTRAIT;
  {
    RenderLock lock;
    portrait_ = portrait;
    cardCount_ = 0;
    activeIndex_ = 0;
    for (const auto& layout : config.layouts) {
      if (cardCount_ >= MAX_CARDS - 1) break;
      Card& card = cards_[cardCount_];
      card.kind = Kind::Layout;
      card.id = layout.id;
      snprintf(card.name, sizeof(card.name), "%s", layout.name.c_str());
      card.title = card.name;
      card.tileCount = config.layoutTiles(layout.id, portrait, card.tiles);
      if (config.activeLayoutId == layout.id) activeIndex_ = cardCount_;
      cardCount_++;
    }
    Card& add = cards_[cardCount_];
    add.kind = Kind::New;
    add.id = 0;
    add.tileCount = 0;
    add.title = tr(STR_DASH_NEW_LAYOUT);
    cardCount_++;

    for (uint8_t i = 0; i < cardCount_; i++) items_[i] = fui::coverGridItem(cards_[i].title, i);
    grid_.count = cardCount_;
  }
  UiListActivity::onEnter();
  moveSelectionTo(activeIndex_);
}

const char* LayoutPickerActivity::headerTitle() const { return tr(STR_DASH_CHANGE_LAYOUT); }

bool LayoutPickerActivity::handleCustomInput() {
  return popup.handleInput(mappedInput, [this] { requestUpdate(); });
}

bool LayoutPickerActivity::handleButtons() {
  // The hold fires mid-press; ActivityManager swallows the release that follows.
  if (mappedInput.wasLongPressed(MappedInputManager::Button::Confirm, EDIT_HOLD_MS)) {
    const int selected = nav.selected;
    if (selected >= 0 && selected < cardCount_) editCard(selected);
    return true;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    const int selected = nav.selected;
    if (selected >= 0 && selected < cardCount_) activateIndex(selected);
    return true;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return true;
  }
  return false;
}

void LayoutPickerActivity::navigateButtons() {
  // One card per step, repeating while held; the grid pages to the selection.
  const int count = listCount();
  const auto next = [this, count] { moveSelectionTo(ButtonNavigator::nextIndex(nav.selected, count)); };
  const auto previous = [this, count] { moveSelectionTo(ButtonNavigator::previousIndex(nav.selected, count)); };
  buttonNavigator.onNextRelease(next);
  buttonNavigator.onPreviousRelease(previous);
  buttonNavigator.onNextContinuous(next);
  buttonNavigator.onPreviousContinuous(previous);
}

void LayoutPickerActivity::activateIndex(const int index) {
  if (index < 0 || index >= cardCount_) return;
  // Selection leaves or covers this screen; a lingering flash would gray an
  // unrelated element on the next render.
  app.clearTapFlash();
  nav.selected = index;
  const Card& card = cards_[index];
  auto& config = DASHBOARD_CONFIG;
  switch (card.kind) {
    case Kind::New:
      if (config.layouts.size() >= DashboardConfigStore::MAX_LAYOUTS) {
        const char* ok = tr(STR_OK_BUTTON);
        popup.show(tr(STR_DASH_NEW_LAYOUT), tr(STR_DASH_LAYOUTS_FULL), &ok, 1, 0, [](int) {});
        requestUpdate();
        return;
      }
      openPortal(*this, renderer, mappedInput, Section::NewLayout);
      return;
    case Kind::Layout:
    default:
      if (!config.activateLayout(card.id)) {
        LOG_ERR("DASH", "No layout %u", static_cast<unsigned>(card.id));
        return;
      }
      break;
  }
  if (!config.saveToFile()) LOG_ERR("DASH", "Could not save the layout choice");
  finish();
}

void LayoutPickerActivity::onRowLongPress(const int index) {
  if (popup.isActive()) return;
  editCard(index);
}

void LayoutPickerActivity::editCard(const int index) {
  if (index < 0 || index >= cardCount_) return;
  const Card& card = cards_[index];
  if (card.kind == Kind::New) {
    activateIndex(index);
    return;
  }
  app.clearTapFlash();
  nav.selected = index;
  openPortal(*this, renderer, mappedInput, Section::EditLayout, card.id);
}

void LayoutPickerActivity::buildScreen(UiScreen& screen) {
  const auto& theme = screen.theme();
  const auto& metrics = UITheme::getInstance().getMetrics();
  // Content below the GUI.drawHeader band, above the button hints, one Home
  // side margin in from the panel edges (the selection ring spills into it).
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.insetContent(fui::Insets{static_cast<int16_t>(metrics.verticalSpacing), sideMargin(), 0, sideMargin()});

  const int helpLineHeight = renderer.getLineHeight(SMALL_FONT_ID);
  const fui::Rect hint = screen.takeBottom(static_cast<int16_t>(helpLineHeight + metrics.verticalSpacing));
  GUI.drawHelpText(renderer, Rect{hint.x, hint.y + metrics.verticalSpacing, hint.width, helpLineHeight},
                   tr(STR_DASH_HOLD_TO_EDIT));

  tileText_ = theme.smallText;
  tileText_.bold = false;
  tileText_.align = fui::TextAlign::Center;
  tileText_.maxLines = 1;
  captionText_ = tileText_;
  const int16_t smallLine = screen.target().lineHeight(tileText_.font);
  const int16_t stroke = px(1);
  captionHeight_ = static_cast<int16_t>(smallLine + 2 * px(CAPTION_PAD) + stroke);

  const uint8_t columns = portrait_ ? PORTRAIT_COLUMNS : LANDSCAPE_COLUMNS;
  const int gridCols = dashboard_layout::columns(portrait_);
  const int gridRows = dashboard_layout::rows(portrait_);
  const int16_t frameGap = selectionFrameGap();
  grid_.columns = columns;
  grid_.selectedCoverFrameGap = frameGap;
  grid_.selectedCoverFrameWidth = selectionFrameWidth();
  grid_.titleText = theme.bodyText;
  grid_.titleText.bold = true;
  grid_.titleText.maxLines = 1;
  grid_.cellInset = fui::Insets{frameGap, 0, 0, 0};
  grid_.labelGap = static_cast<int16_t>(frameGap + theme.spaceSm);
  grid_.labelHeight = screen.target().lineHeight(grid_.titleText.font);
  grid_.gap = px(COLUMN_GAP);
  grid_.rowGap = px(ROW_GAP);

  // A card is the thumbnail, in the dashboard's proportions, over its caption
  // band; a cell adds the ring's clearance above and the name below. Cards
  // take the full column width unless one row would not fit the height.
  const fui::Rect band = screen.body();
  const int cellChrome = frameGap + grid_.labelGap + grid_.labelHeight;
  const int indicator = grid_.scrollIndicatorWidth + grid_.scrollIndicatorGap;
  int cardWidth = std::max(1, (band.width - indicator - (columns - 1) * grid_.gap) / columns);
  int cardHeight = cardWidth * gridRows / gridCols + captionHeight_;
  if (cardHeight + cellChrome > band.height) {
    cardHeight = std::max<int>(captionHeight_ + 1, band.height - cellChrome);
    cardWidth = std::max(1, (cardHeight - captionHeight_) * gridCols / gridRows);
  }
  grid_.coverSize = fui::Size{static_cast<int16_t>(cardWidth), static_cast<int16_t>(cardHeight)};
  grid_.rowHeight = static_cast<int16_t>(cardHeight + cellChrome);

  const int rowsPerPage = std::max(1, (band.height + grid_.rowGap) / (grid_.rowHeight + grid_.rowGap));
  const int perPage = rowsPerPage * columns;
  const bool overflows = cardCount_ > perPage;
  // Centred as a block (a full page's height, so pages line up): EqualWidth
  // columns in a rect exactly this wide put each card at its cell's edge.
  const int gridWidth = columns * cardWidth + (columns - 1) * grid_.gap + (overflows ? indicator : 0);
  const int gridHeight = rowsPerPage * grid_.rowHeight + (rowsPerPage - 1) * grid_.rowGap;
  const fui::Rect gridRect{static_cast<int16_t>(band.x + std::max(0, (band.width - gridWidth) / 2)),
                           static_cast<int16_t>(band.y + std::max(0, (band.height - gridHeight) / 2)),
                           static_cast<int16_t>(gridWidth), static_cast<int16_t>(gridHeight)};

  const int selected = std::clamp(nav.selected.load(), 0, std::max(0, cardCount_ - 1));
  grid_.selectedIndex = static_cast<int16_t>(selected);
  grid_.topIndex = fui::coverGridTopIndexFor(static_cast<uint16_t>(selected), cardCount_, columns,
                                             static_cast<uint16_t>(perPage));
  fui::coverGrid(screen.frame(), gridRect, grid_);
}

bool LayoutPickerActivity::paintCard(fui::DrawTarget& target, const fui::Rect rect, const uint16_t index) {
  if (index >= cardCount_) return false;
  const Card& card = cards_[index];
  // Home's framed cover: an ink outline with a drop shadow right and below.
  const int16_t shadow = px(2);
  const int16_t stroke = px(1);
  const auto ink = fui::Paint::solid(fui::Color::Black);
  target.fill(rect, fui::Paint::solid(fui::Color::White));
  target.fill(fui::Rect{rect.right(), static_cast<int16_t>(rect.y + shadow), shadow, rect.height}, ink);
  target.fill(fui::Rect{static_cast<int16_t>(rect.x + shadow), rect.bottom(), rect.width, shadow}, ink);
  target.stroke(rect, ink, static_cast<uint8_t>(stroke), 0);

  if (card.kind == Kind::New) {
    // A plus sign.
    const int16_t arm = static_cast<int16_t>(std::min(rect.width, rect.height) / 6);
    const int16_t thickness = px(6);
    const int16_t cx = static_cast<int16_t>(rect.x + rect.width / 2);
    const int16_t cy = static_cast<int16_t>(rect.y + rect.height / 2);
    const auto across = static_cast<int16_t>(2 * arm);
    const fui::Rect bar{static_cast<int16_t>(cx - arm), static_cast<int16_t>(cy - thickness / 2), across, thickness};
    const fui::Rect post{static_cast<int16_t>(cx - thickness / 2), static_cast<int16_t>(cy - arm), thickness, across};
    target.fill(bar, ink);
    target.fill(post, ink);
    return true;
  }

  // Caption band along the bottom: "In use" inverted on the active layout,
  // else what kind of layout it is under a rule.
  const bool active = index == activeIndex_;
  const fui::Rect caption{rect.x, static_cast<int16_t>(rect.bottom() - captionHeight_), rect.width, captionHeight_};
  fui::TextStyle captionStyle = captionText_;
  if (active) {
    target.fill(caption, ink);
    captionStyle.color = fui::Color::White;
    captionStyle.bold = true;
  } else {
    target.fill(fui::Rect{caption.x, caption.y, caption.width, stroke}, ink);
  }
  char tiles[24];
  snprintf(tiles, sizeof(tiles), tr(STR_DASH_TILE_COUNT_FORMAT), static_cast<unsigned>(card.tileCount));
  const char* note = active ? tr(STR_DASH_IN_USE) : tiles;
  target.text(caption.inset(fui::Insets{stroke, px(CAPTION_PAD), 0, px(CAPTION_PAD)}), note, captionStyle);

  const int16_t pad = px(THUMB_PAD);
  const fui::Rect thumbnail{static_cast<int16_t>(rect.x + pad), static_cast<int16_t>(rect.y + pad),
                            static_cast<int16_t>(rect.width - 2 * pad),
                            static_cast<int16_t>(caption.y - rect.y - 2 * pad)};
  drawTiles(target, thumbnail, card);
  return true;
}

void LayoutPickerActivity::drawTiles(fui::DrawTarget& target, const fui::Rect area, const Card& card) const {
  if (area.empty()) return;
  const int cols = dashboard_layout::columns(portrait_);
  const int rows = dashboard_layout::rows(portrait_);
  // Square cells: the grid as large as the area allows, centred in it.
  int width = area.width;
  int height = width * rows / cols;
  if (height > area.height) {
    height = area.height;
    width = height * cols / rows;
  }
  const int left = area.x + (area.width - width) / 2;
  const int top = area.y + (area.height - height) / 2;
  const int16_t gap = px(TILE_GAP);
  const int16_t textPad = px(TILE_TEXT_PAD);
  const auto stroke = static_cast<uint8_t>(px(1));
  const auto radius = static_cast<uint8_t>(px(3));
  const auto ink = fui::Paint::solid(fui::Color::Black);
  for (uint8_t i = 0; i < card.tileCount; i++) {
    const DashboardTile& tile = card.tiles[i];
    const int x0 = left + tile.x * width / cols;
    const int x1 = left + (tile.x + tile.width) * width / cols;
    const int y0 = top + tile.y * height / rows;
    const int y1 = top + (tile.y + tile.height) * height / rows;
    const fui::Rect cell{static_cast<int16_t>(x0 + gap), static_cast<int16_t>(y0 + gap),
                         static_cast<int16_t>(x1 - x0 - 2 * gap), static_cast<int16_t>(y1 - y0 - 2 * gap)};
    if (cell.empty()) continue;
    target.stroke(cell, ink, stroke, radius);
    target.text(cell.inset(fui::Insets{0, textPad, 0, textPad}), dashboard_widgets::widgetName(tile.widget),
                tileText_);
  }
}

void LayoutPickerActivity::render(RenderLock&&) {
  renderer.clearScreen();
  drawChrome();
  renderUi();
  // The notice draws its own hints over the grid.
  if (popup.processRender(renderer, mappedInput)) return;
  drawFooter();
  renderer.displayBuffer();
}
