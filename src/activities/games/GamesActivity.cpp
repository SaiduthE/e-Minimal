#include "GamesActivity.h"

#include <Game.h>
#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>

#include "GameBoards.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace fui = freeink::ui;

namespace {
// Hand-placed sizes are written at 1x; the panel's UI scale applies.
int16_t px(const int v) { return static_cast<int16_t>(UITheme::scaledPx(v)); }

// The Home grid's selection ring and page margin (EMinimalHomeUi): a solid
// band whose outer edge sits the frame gap outside the tile, inside a margin
// wide enough to hold it.
int16_t selectionFrameGap() { return px(10); }
int16_t selectionFrameWidth() { return px(4); }
int16_t sideMargin() { return px(24); }

constexpr int COLUMN_GAP = 48;
constexpr int ART_PAD = 16;
constexpr int CAPTION_PAD = 8;
// Tiles stay card-shaped: neither side longer than 5/4 of the other.
constexpr int TILE_ASPECT_LONG = 5;
constexpr int TILE_ASPECT_SHORT = 4;

const char* descriptionFor(const party::GameId id) {
  switch (id) {
    case party::GameId::Undercover:
      return tr(STR_GAME_UNDERCOVER_DESC);
    case party::GameId::Codenames:
      return tr(STR_GAME_CODENAMES_DESC);
    case party::GameId::Battleship:
      return tr(STR_GAME_BATTLESHIP_DESC);
    case party::GameId::Ludo:
      return tr(STR_GAME_LUDO_DESC);
    default:
      return "";
  }
}
}  // namespace

GamesActivity::GamesActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("Games", renderer, mappedInput) {
  for (uint8_t i = 0; i < party::GAME_COUNT; i++) {
    const party::GameId id = party::gameAt(i);
    const party::GameMeta& meta = party::gameMeta(id);
    if (meta.minPlayers == meta.maxPlayers) {
      snprintf(seats_[i], SEATS_LENGTH, tr(STR_GAMES_SEATS_EXACT_FORMAT), static_cast<unsigned>(meta.minPlayers));
    } else {
      snprintf(seats_[i], SEATS_LENGTH, tr(STR_GAMES_SEATS_RANGE_FORMAT), static_cast<unsigned>(meta.minPlayers),
               static_cast<unsigned>(meta.maxPlayers));
    }
    tiles_[i] = fui::coverGridItem(games::titleFor(id), i);
  }

  grid_.items = tiles_;
  grid_.count = party::GAME_COUNT;
  grid_.columns = GRID_COLUMNS;
  grid_.action = ACTION_ROW;
  grid_.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  grid_.selectionIndicator = fui::CoverGridSelectionIndicator::CoverFrame;
  // Plain cells: the tile paints its own card and the ring marks the selection.
  grid_.cellStyles.explicitlySet = true;
  grid_.cellStyles.selected.border = fui::Paint::solid(fui::Color::Black);
  grid_.labelAlign = fui::TextAlign::Center;
  grid_.labelFollowsCover = true;
  grid_.scrollIndicator = false;
  grid_.coverPainterUserData = this;
  grid_.coverPainter = [](fui::DrawTarget& target, fui::Rect tile, const fui::CoverGridItem&, uint16_t index,
                          void* user) {
    return static_cast<GamesActivity*>(user)->paintTile(target, tile, index);
  };
}

const char* GamesActivity::headerTitle() const { return tr(STR_APP_GAMES); }

void GamesActivity::activateIndex(const int index) {
  // Selection leaves this screen; a lingering flash would gray an unrelated
  // element on the next render.
  app.clearTapFlash();
  nav.selected = index;
  activityManager.goToGameNight(party::gameAt(static_cast<uint8_t>(index)));
}

void GamesActivity::drawChrome() {
  // The header spans the safe area: in landscape the hints run down a side.
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  GUI.drawHeader(renderer, Rect{safe.x, safe.y + metrics.topPadding, safe.width, metrics.headerHeight}, headerTitle());
}

void GamesActivity::buildScreen(UiScreen& screen) {
  const auto& theme = screen.theme();
  const auto& metrics = UITheme::getInstance().getMetrics();
  // Content below the header, inside the safe area that keeps clear of the
  // button hints on whichever edge they run (the bottom only in portrait), and
  // one spacing short of them. The Home side margin holds the selection ring.
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  screen.setContentMarginFromScreen(
      fui::Insets{static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
                  static_cast<int16_t>(renderer.getScreenWidth() - safe.x - safe.width),
                  static_cast<int16_t>(renderer.getScreenHeight() - safe.y - safe.height + metrics.verticalSpacing),
                  static_cast<int16_t>(safe.x)});
  screen.insetContent(fui::Insets{static_cast<int16_t>(metrics.verticalSpacing), sideMargin(), 0, sideMargin()});

  const int selected = nav.selected.load();
  const bool hasSelection = selected >= 0 && selected < party::GAME_COUNT;

  // Help band, bottom up: the hotspot hint, then the selected game's description.
  auto hint = theme.smallText;
  hint.align = fui::TextAlign::Center;
  const fui::Rect hintRect = screen.takeBottom(screen.target().lineHeight(hint.font), theme.spaceMd);
  screen.target().text(hintRect, tr(STR_GAMES_PICK_HINT), hint);
  auto about = theme.bodyText;
  about.bold = false;
  about.align = fui::TextAlign::Center;
  about.maxLines = 2;
  const fui::Rect aboutRect =
      screen.takeBottom(static_cast<int16_t>(screen.target().lineHeight(about.font) * about.maxLines), theme.spaceMd);
  if (hasSelection) {
    screen.target().text(aboutRect, descriptionFor(party::gameAt(static_cast<uint8_t>(selected))), about);
  }

  // The tiles take the rest, 2x2 in either orientation. A cell is the ring's
  // clearance above the tile, the tile, then its title below.
  const int16_t frameGap = selectionFrameGap();
  grid_.selectedCoverFrameGap = frameGap;
  grid_.selectedCoverFrameWidth = selectionFrameWidth();
  grid_.titleText = theme.bodyText;
  grid_.titleText.bold = true;
  grid_.titleText.maxLines = 1;
  grid_.cellInset = fui::Insets{frameGap, 0, 0, 0};
  grid_.labelGap = static_cast<int16_t>(frameGap + theme.spaceSm);
  grid_.labelHeight = screen.target().lineHeight(grid_.titleText.font);
  grid_.gap = px(COLUMN_GAP);
  const fui::Rect band = screen.body();
  grid_.rowGap = static_cast<int16_t>(std::max<int>(theme.spaceSm, band.height * 2 / 100));
  const int cellChrome = frameGap + grid_.labelGap + grid_.labelHeight;
  const int maxWidth = std::max(1, (band.width - (GRID_COLUMNS - 1) * grid_.gap) / GRID_COLUMNS);
  const int maxHeight = std::max(1, (band.height - (GRID_ROWS - 1) * grid_.rowGap) / GRID_ROWS - cellChrome);
  const int tileHeight = std::max(1, std::min(maxHeight, maxWidth * TILE_ASPECT_LONG / TILE_ASPECT_SHORT));
  const int tileWidth = std::max(1, std::min(maxWidth, tileHeight * TILE_ASPECT_LONG / TILE_ASPECT_SHORT));
  grid_.coverSize = fui::Size{static_cast<int16_t>(tileWidth), static_cast<int16_t>(tileHeight)};
  grid_.rowHeight = static_cast<int16_t>(tileHeight + cellChrome);
  // Centred as a block: EqualWidth columns in a rect exactly two tiles and one
  // gutter wide put each tile at its cell's edge.
  const int gridWidth = GRID_COLUMNS * tileWidth + (GRID_COLUMNS - 1) * grid_.gap;
  const int gridHeight = GRID_ROWS * grid_.rowHeight + (GRID_ROWS - 1) * grid_.rowGap;
  const fui::Rect gridRect{static_cast<int16_t>(band.x + (band.width - gridWidth) / 2),
                           static_cast<int16_t>(band.y + std::max(0, (band.height - gridHeight) / 2)),
                           static_cast<int16_t>(gridWidth), static_cast<int16_t>(gridHeight)};
  grid_.selectedIndex = static_cast<int16_t>(hasSelection ? selected : -1);
  fui::coverGrid(screen.frame(), gridRect, grid_);
}

bool GamesActivity::paintTile(fui::DrawTarget& target, const fui::Rect tile, const uint16_t index) {
  if (index >= party::GAME_COUNT) return false;
  // Home's framed cover (EMinimalHomeUi::paintFramedCover): an ink outline
  // with a drop shadow right and below.
  const int16_t shadow = px(2);
  const int16_t stroke = px(1);
  const auto ink = fui::Paint::solid(fui::Color::Black);
  target.fill(fui::Rect{tile.right(), static_cast<int16_t>(tile.y + shadow), shadow, tile.height}, ink);
  target.fill(fui::Rect{static_cast<int16_t>(tile.x + shadow), tile.bottom(), tile.width, shadow}, ink);

  // Caption band along the bottom: the seat range under a rule.
  const int lineHeight = renderer.getLineHeight(SMALL_FONT_ID);
  const int captionTop = tile.bottom() - lineHeight - 2 * px(CAPTION_PAD) - stroke;
  target.fill(fui::Rect{tile.x, static_cast<int16_t>(captionTop), tile.width, stroke}, ink);
  const char* seats = seats_[index];
  renderer.drawText(SMALL_FONT_ID, tile.x + (tile.width - renderer.getTextWidth(SMALL_FONT_ID, seats)) / 2,
                    captionTop + stroke + px(CAPTION_PAD), seats);

  const int pad = px(ART_PAD);
  games::drawThumbnail(renderer, party::gameAt(static_cast<uint8_t>(index)),
                       Rect(tile.x + pad, tile.y + pad, tile.width - 2 * pad, captionTop - tile.y - 2 * pad));
  target.stroke(tile, ink, stroke, 0);
  return true;
}

void GamesActivity::drawFooter() {
  const auto labels = mappedInput.mapLabels("", tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}
