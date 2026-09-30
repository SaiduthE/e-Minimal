#include "GameBoards.h"

#include <I18n.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "components/UITheme.h"
#include "fontIds.h"

namespace games {

namespace {

using party::Battleship;
using party::Codenames;
using party::GameSession;
using party::Ludo;
using party::Undercover;

constexpr int TILE_GAP = 4;

// Hand-placed spacing below is written at 1x; the panel's UI scale applies.
int px(const int v) { return UITheme::scaledPx(v); }

// Centres one line of text in a box. Everything drawn here is a short label, so
// there is no wrapping: the caller sizes the box from getTextWidth() when it
// matters.
void centerText(const GfxRenderer& renderer, const int fontId, const Rect& box, const char* text,
                const bool black = true, const EpdFontFamily::Style style = EpdFontFamily::REGULAR) {
  const int width = renderer.getTextWidth(fontId, text, style);
  const int x = box.x + (box.width - width) / 2;
  const int y = box.y + (box.height - renderer.getLineHeight(fontId)) / 2;
  renderer.drawText(fontId, x, y, text, black, style);
}

// Picks the largest UI font whose rendering of `text` still fits `maxWidth`.
int fontThatFits(const GfxRenderer& renderer, const char* text, const int maxWidth) {
  if (renderer.getTextWidth(UI_10_FONT_ID, text) <= maxWidth) return UI_10_FONT_ID;
  return SMALL_FONT_ID;
}

// `weight` multiplies the outline widths, for the larger picker thumbnails.
void drawSwatch(const GfxRenderer& renderer, const Rect& box, const uint8_t style, const int weight = 1) {
  switch (style) {
    case 0:  // solid
      renderer.fillRect(box.x, box.y, box.width, box.height, true);
      break;
    case 1:  // outline
      renderer.drawRect(box.x, box.y, box.width, box.height, 2 * weight, true);
      break;
    case 2:  // light hatch
      renderer.fillRectDither(box.x, box.y, box.width, box.height, Color::LightGray);
      renderer.drawRect(box.x, box.y, box.width, box.height, weight, true);
      break;
    default:  // dark hatch
      renderer.fillRectDither(box.x, box.y, box.width, box.height, Color::DarkGray);
      renderer.drawRect(box.x, box.y, box.width, box.height, weight, true);
      break;
  }
}

const char* seatName(const GameSession& session, const uint8_t seat) {
  static char fallback[12];
  if (session.isSeated(seat)) return session.player(seat).name;
  snprintf(fallback, sizeof(fallback), "Seat %u", seat + 1);
  return fallback;
}

void undercoverStatus(const GameSession& session, const Undercover& game, char* out, const size_t cap) {
  switch (game.phase()) {
    case Undercover::Phase::Describe:
      snprintf(out, cap, tr(STR_UC_DESCRIBE_FORMAT), game.roundNumber());
      return;
    case Undercover::Phase::Vote:
      snprintf(out, cap, tr(STR_UC_VOTE_FORMAT), game.roundNumber());
      return;
    case Undercover::Phase::Reveal: {
      const int8_t out_seat = game.lastEliminated();
      if (out_seat < 0) {
        snprintf(out, cap, "%s", tr(STR_UC_TIE_VOTE));
      } else {
        const uint8_t seat = static_cast<uint8_t>(out_seat);
        snprintf(
            out, cap,
            I18N.get(game.isUndercover(seat) ? StrId::STR_UC_OUT_UNDERCOVER_FORMAT : StrId::STR_UC_OUT_CIVILIAN_FORMAT),
            seatName(session, seat));
      }
      return;
    }
    case Undercover::Phase::Over:
      snprintf(out, cap, "%s",
               I18N.get(game.winner() == 1 ? StrId::STR_UC_CIVILIANS_WIN : StrId::STR_UC_UNDERCOVER_WINS));
      return;
  }
}

void codenamesStatus(const Codenames& game, char* out, const size_t cap) {
  const char* onTurn = I18N.get(game.turn() == Codenames::RED ? StrId::STR_CN_TEAM_RED : StrId::STR_CN_TEAM_BLUE);
  switch (game.phase()) {
    case Codenames::Phase::Clue:
      snprintf(out, cap, tr(STR_CN_CLUE_FORMAT), onTurn);
      return;
    case Codenames::Phase::Guess:
      if (game.unlimited()) {
        const bool zero = game.clueCount() == 0;
        snprintf(out, cap, tr(STR_CN_GUESS_OPEN_FORMAT), onTurn, game.clue(), zero ? "0" : tr(STR_CN_UNLIMITED));
        return;
      }
      snprintf(out, cap, tr(STR_CN_GUESS_FORMAT), onTurn, game.clue(), game.clueCount(), game.guessesLeft());
      return;
    case Codenames::Phase::Over:
      snprintf(out, cap, tr(STR_CN_WINS_FORMAT),
               I18N.get(game.winner() == Codenames::RED ? StrId::STR_CN_TEAM_RED : StrId::STR_CN_TEAM_BLUE));
      return;
  }
}

void battleshipStatus(const GameSession& session, const Battleship& game, char* out, const size_t cap) {
  switch (game.phase()) {
    case Battleship::Phase::Place:
      snprintf(out, cap, "%s", tr(STR_BS_PLACING));
      return;
    case Battleship::Phase::Fire:
      snprintf(out, cap, tr(STR_BS_TURN_FORMAT), seatName(session, game.turn()));
      return;
    case Battleship::Phase::Over:
      snprintf(out, cap, tr(STR_BS_WINS_FORMAT),
               seatName(session, static_cast<uint8_t>(std::max<int8_t>(game.winner(), 0))));
      return;
  }
}

void ludoStatus(const GameSession& session, const Ludo& game, char* out, const size_t cap) {
  switch (game.phase()) {
    case Ludo::Phase::Pick:
      snprintf(out, cap, "%s", tr(STR_LUDO_PICK));
      return;
    case Ludo::Phase::Roll:
      snprintf(out, cap, tr(STR_LUDO_ROLL_FORMAT), seatName(session, game.turn()));
      return;
    case Ludo::Phase::Move:
      snprintf(out, cap, tr(STR_LUDO_MOVE_FORMAT), seatName(session, game.turn()), game.die());
      return;
    case Ludo::Phase::Over:
      snprintf(out, cap, tr(STR_LUDO_WINS_FORMAT),
               seatName(session, static_cast<uint8_t>(std::max<int8_t>(game.winner(), 0))));
      return;
  }
}

// --- undercover --------------------------------------------------------------

void drawUndercover(const GfxRenderer& renderer, const GameSession& session, const Undercover& game,
                    const Rect& content) {
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const int rowHeight = lineHeight + px(14);
  const int gap = px(TILE_GAP);
  const int columns = content.width > 2 * content.height ? 2 : 1;
  const int columnWidth = (content.width - (columns - 1) * gap) / columns;

  for (uint8_t seat = 0; seat < game.seats(); seat++) {
    const int column = columns == 1 ? 0 : seat % columns;
    const int row = columns == 1 ? seat : seat / columns;
    const Rect box(content.x + column * (columnWidth + gap), content.y + row * rowHeight, columnWidth, rowHeight - gap);
    if (box.y + box.height > content.y + content.height) break;

    const bool out = !game.alive(seat);
    renderer.drawRect(box.x, box.y, box.width, box.height, out ? 1 : 2, true);

    char label[40];
    snprintf(label, sizeof(label), "%u  %s", seat + 1, session.player(seat).name);
    renderer.drawText(UI_10_FONT_ID, box.x + px(10), box.y + (box.height - lineHeight) / 2, label, true,
                      out ? EpdFontFamily::ITALIC : EpdFontFamily::REGULAR);

    char right[24];
    if (out) {
      snprintf(right, sizeof(right), "out%s", game.isUndercover(seat) ? " - undercover" : "");
    } else if (game.phase() == Undercover::Phase::Describe) {
      snprintf(right, sizeof(right), "%s", game.ready(seat) ? "ready" : "...");
    } else {
      snprintf(right, sizeof(right), "%u vote%s", game.votesAgainst(seat), game.votesAgainst(seat) == 1 ? "" : "s");
    }
    const int rightWidth = renderer.getTextWidth(SMALL_FONT_ID, right);
    renderer.drawText(SMALL_FONT_ID, box.x + box.width - rightWidth - px(10),
                      box.y + (box.height - renderer.getLineHeight(SMALL_FONT_ID)) / 2, right);

    if (out) {
      const int middle = box.y + box.height / 2;
      renderer.drawLine(box.x + px(6), middle, box.x + box.width - px(6), middle, true);
    }
  }
}

// --- codenames ---------------------------------------------------------------

void drawCodenames(const GfxRenderer& renderer, const Codenames& game, const Rect& content) {
  const int legendHeight = renderer.getLineHeight(SMALL_FONT_ID) + px(10);
  const int gridHeight = content.height - legendHeight;
  const int gap = px(TILE_GAP);
  const int crossInset = px(4);
  const int tileWidth = (content.width - (Codenames::GRID - 1) * gap) / Codenames::GRID;
  const int tileHeight = (gridHeight - (Codenames::GRID - 1) * gap) / Codenames::GRID;

  for (uint8_t tile = 0; tile < Codenames::TILES; tile++) {
    const int column = tile % Codenames::GRID;
    const int row = tile / Codenames::GRID;
    const Rect box(content.x + column * (tileWidth + gap), content.y + row * (tileHeight + gap), tileWidth, tileHeight);
    const char* tileText = game.tileWord(tile);
    const int fontId = fontThatFits(renderer, tileText, box.width - px(8));

    if (!game.revealed(tile)) {
      renderer.drawRect(box.x, box.y, box.width, box.height, 1, true);
      centerText(renderer, fontId, box, tileText);
      continue;
    }

    switch (game.owner(tile)) {
      case Codenames::RED:
        renderer.fillRect(box.x, box.y, box.width, box.height, true);
        centerText(renderer, fontId, box, tileText, false, EpdFontFamily::BOLD);
        break;
      case Codenames::BLUE:
        renderer.fillRectDither(box.x, box.y, box.width, box.height, Color::LightGray);
        renderer.drawRect(box.x, box.y, box.width, box.height, 2, true);
        centerText(renderer, fontId, box, tileText, true, EpdFontFamily::BOLD);
        break;
      case Codenames::ASSASSIN:
        renderer.fillRect(box.x, box.y, box.width, box.height, true);
        renderer.drawLine(box.x + crossInset, box.y + crossInset, box.x + box.width - crossInset,
                          box.y + box.height - crossInset, 2, false);
        renderer.drawLine(box.x + box.width - crossInset, box.y + crossInset, box.x + crossInset,
                          box.y + box.height - crossInset, 2, false);
        centerText(renderer, fontId, box, tileText, false, EpdFontFamily::BOLD);
        break;
      default:
        renderer.drawRect(box.x, box.y, box.width, box.height, 1, true);
        centerText(renderer, fontId, box, tileText);
        renderer.drawLine(box.x + px(6), box.y + box.height / 2, box.x + box.width - px(6), box.y + box.height / 2,
                          true);
        break;
    }
  }

  // Legend: on a monochrome panel the teams are a fill pattern, so say which.
  const int legendY = content.y + gridHeight + px(4);
  const int swatch = renderer.getLineHeight(SMALL_FONT_ID) - px(2);
  int x = content.x;
  const auto legendEntry = [&](const uint8_t style, const char* text) {
    drawSwatch(renderer, Rect(x, legendY, swatch, swatch), style);
    x += swatch + px(4);
    renderer.drawText(SMALL_FONT_ID, x, legendY, text);
    x += renderer.getTextWidth(SMALL_FONT_ID, text) + px(14);
  };

  char red[24];
  char blue[24];
  snprintf(red, sizeof(red), "Red %u left", game.remaining(Codenames::RED));
  snprintf(blue, sizeof(blue), "Blue %u left", game.remaining(Codenames::BLUE));
  legendEntry(0, red);
  legendEntry(2, blue);
  legendEntry(1, "bystander");
}

// --- battleship --------------------------------------------------------------

void drawFleetGrid(const GfxRenderer& renderer, const Battleship& game, const uint8_t player, const Rect& box,
                   const char* title) {
  const int titleHeight = renderer.getLineHeight(SMALL_FONT_ID) + px(2);
  const int labelWidth = renderer.getTextWidth(SMALL_FONT_ID, "10") + px(3);
  // The "N afloat" line under the grid is reserved before the grid is sized.
  const int footerHeight = renderer.getLineHeight(SMALL_FONT_ID) + px(2);
  const int available = std::min(box.width - labelWidth, box.height - titleHeight * 2 - footerHeight);
  const int cell = available / Battleship::SIZE;
  const int gridLeft = box.x + labelWidth;
  const int gridTop = box.y + titleHeight * 2;

  renderer.drawText(SMALL_FONT_ID, box.x, box.y, title, true, EpdFontFamily::BOLD);

  for (uint8_t i = 0; i < Battleship::SIZE; i++) {
    char label[3];
    snprintf(label, sizeof(label), "%c", static_cast<char>('A' + i));
    centerText(renderer, SMALL_FONT_ID, Rect(gridLeft + i * cell, gridTop - titleHeight, cell, titleHeight), label);
    snprintf(label, sizeof(label), "%u", i + 1);
    const int width = renderer.getTextWidth(SMALL_FONT_ID, label);
    renderer.drawText(SMALL_FONT_ID, gridLeft - width - px(3),
                      gridTop + i * cell + (cell - renderer.getLineHeight(SMALL_FONT_ID)) / 2, label);
  }

  const int sunkPip = px(2);
  const int missPip = px(3);
  for (uint8_t y = 0; y < Battleship::SIZE; y++) {
    for (uint8_t x = 0; x < Battleship::SIZE; x++) {
      const Rect cellBox(gridLeft + x * cell, gridTop + y * cell, cell, cell);
      renderer.drawRect(cellBox.x, cellBox.y, cellBox.width, cellBox.height, 1, true);
      switch (game.shotAt(player, x, y)) {
        case Battleship::HIT:
          renderer.fillRect(cellBox.x + 1, cellBox.y + 1, cellBox.width - 2, cellBox.height - 2, true);
          if (game.sunkAt(player, x, y)) {
            // A sunk ship's cells keep a white pip so a whole hull reads as one
            // shape instead of a blob of separate hits.
            renderer.fillRect(cellBox.x + (cellBox.width - sunkPip) / 2, cellBox.y + (cellBox.height - sunkPip) / 2,
                              sunkPip, sunkPip, false);
          }
          break;
        case Battleship::MISS:
          renderer.fillRect(cellBox.x + (cellBox.width - missPip) / 2, cellBox.y + (cellBox.height - missPip) / 2,
                            missPip, missPip, true);
          break;
        default:
          break;
      }
    }
  }

  char footer[32];
  snprintf(footer, sizeof(footer), "%u afloat%s", game.shipsLeft(player),
           game.phase() == Battleship::Phase::Place ? (game.ready(player) ? " - ready" : " - placing") : "");
  renderer.drawText(SMALL_FONT_ID, box.x, gridTop + Battleship::SIZE * cell + px(2), footer);
}

void drawBattleship(const GfxRenderer& renderer, const GameSession& session, const Battleship& game,
                    const Rect& content) {
  const bool sideBySide = content.width >= content.height;
  const int gap = px(TILE_GAP * 4);
  const int halfWidth = sideBySide ? (content.width - gap) / 2 : content.width;
  const int halfHeight = sideBySide ? content.height : (content.height - gap) / 2;

  for (uint8_t player = 0; player < Battleship::PLAYERS; player++) {
    char title[32];
    snprintf(title, sizeof(title), "%s%s", session.isSeated(player) ? session.player(player).name : "empty seat",
             game.phase() == Battleship::Phase::Fire && game.turn() == player ? "  <- to fire" : "");
    const Rect box(content.x + (sideBySide ? player * (halfWidth + gap) : 0),
                   content.y + (sideBySide ? 0 : player * (halfHeight + gap)), halfWidth, halfHeight);
    drawFleetGrid(renderer, game, player, box, title);
  }
}

// --- ludo --------------------------------------------------------------------

// A Ludo player is a shape, in Ludo::shapeName order: circle, square, triangle,
// diamond. Each is drawn in a square box: the circle and the diamond touch its
// edges, the square keeps 7/8 of it so it does not outweigh the others, and the
// triangle points up with its base along the bottom.
constexpr uint8_t TRIANGLE = 2;

// Fills a shape row by row where its edges slant, so the hatches work as well
// as ink and paper.
void fillShape(const GfxRenderer& renderer, const uint8_t shape, const Rect& box, const Color color) {
  switch (shape) {
    case 0:
      renderer.fillRoundedRect(box.x, box.y, box.width, box.height, box.width / 2, color);
      return;
    case 1: {
      const int inset = box.width / 16;
      renderer.fillRectDither(box.x + inset, box.y + inset, box.width - 2 * inset, box.height - 2 * inset, color);
      return;
    }
    case TRIANGLE:
      for (int row = 0; row < box.height; row++) {
        const int span = box.width * (row + 1) / box.height;
        renderer.fillRectDither(box.x + (box.width - span) / 2, box.y + row, span, 1, color);
      }
      return;
    default:
      for (int row = 0; row < box.height; row++) {
        const int fromMiddle = std::abs(2 * row - (box.height - 1));
        const int span = box.width * (box.height - fromMiddle) / box.height;
        renderer.fillRectDither(box.x + (box.width - span) / 2, box.y + row, span, 1, color);
      }
      return;
  }
}

// Radius of the triangle's inscribed circle: where its digit goes, and how its
// outline scales in.
int triangleInradius(const Rect& box) {
  const float half = box.width / 2.0f;
  const float height = static_cast<float>(box.height);
  const float slant = std::sqrt(half * half + height * height);
  return static_cast<int>(box.width * height / (box.width + 2 * slant));
}

// The same shape `stroke` in from its edges: ink, then this, draws an outline.
Rect insetShape(const uint8_t shape, const Rect& box, const int stroke) {
  switch (shape) {
    case TRIANGLE: {
      // Scaled about the inscribed circle's centre, `radius` above the base.
      const int radius = triangleInradius(box);
      if (radius <= stroke) return Rect();
      const int width = box.width * (radius - stroke) / radius;
      const int height = box.height * (radius - stroke) / radius;
      return Rect(box.x + (box.width - width) / 2, box.y + box.height - stroke - height, width, height);
    }
    case 1: {
      // fillShape keeps 7/8 of the box, so the box moves in 8/7 of the stroke.
      const int inset = (stroke * 8 + 6) / 7;
      return Rect(box.x + inset, box.y + inset, box.width - 2 * inset, box.height - 2 * inset);
    }
    case 3: {
      const int inset = stroke * 3 / 2;  // the edges run at 45 degrees
      return Rect(box.x + inset, box.y + inset, box.width - 2 * inset, box.height - 2 * inset);
    }
    default:
      return Rect(box.x + stroke, box.y + stroke, box.width - 2 * stroke, box.height - 2 * stroke);
  }
}

// A shape filled with `fill` inside an ink outline `stroke` wide.
void drawShape(const GfxRenderer& renderer, const uint8_t shape, const Rect& box, const Color fill, const int stroke) {
  fillShape(renderer, shape, box, Color::Black);
  const Rect inner = insetShape(shape, box, stroke);
  if (inner.width > 0 && inner.height > 0) fillShape(renderer, shape, inner, fill);
}

// One token: the owner's shape in paper with a bold ink outline and the token's
// own number, 1-4, in bold. More of the owner's tokens on the same square show
// as an ink copy of the shape offset behind it.
void drawLudoToken(const GfxRenderer& renderer, const uint8_t shape, const uint8_t token, const Rect& cellBox,
                   const bool pile) {
  // The triangle takes more of the cell: its number sits in its lower middle.
  const int inset = shape == TRIANGLE ? std::max(1, cellBox.width / 32) : std::max(2, cellBox.width / 10);
  const int side = std::min(cellBox.width, cellBox.height) - 2 * inset;
  if (side < 8) return;
  // A pile spreads the two copies by `shift` each way, which the inset has to
  // absorb or they leave the cell (the triangle's inset is the thin one).
  const int shift = pile ? std::min(inset, std::max(2, side / 16)) : 0;
  const Rect box(cellBox.x + (cellBox.width - side) / 2 - shift, cellBox.y + (cellBox.height - side) / 2 - shift, side,
                 side);
  if (pile) fillShape(renderer, shape, Rect(box.x + 2 * shift, box.y + 2 * shift, side, side), Color::Black);
  drawShape(renderer, shape, box, Color::White, std::max(2, side / 14));

  Rect number = box;
  if (shape == TRIANGLE) {
    const int radius = triangleInradius(box);
    number = Rect(box.x + box.width / 2 - radius, box.y + box.height - 2 * radius, 2 * radius, 2 * radius);
  }
  const char digit[2] = {static_cast<char>('1' + token), '\0'};
  centerText(renderer, UI_10_FONT_ID, number, digit, true, EpdFontFamily::BOLD);
}

// A house in `ink` holding `count`, the tokens home, in the other colour.
void drawHomeBadge(const GfxRenderer& renderer, const Rect& box, const uint8_t count, const bool ink) {
  const int roof = box.height * 2 / 5;
  for (int row = 0; row < roof; row++) {
    const int span = box.width * (row + 1) / roof;
    renderer.fillRect(box.x + (box.width - span) / 2, box.y + row, span, 1, ink);
  }
  const int eave = box.width / 8;
  const Rect walls(box.x + eave, box.y + roof, box.width - 2 * eave, box.height - roof);
  renderer.fillRect(walls.x, walls.y, walls.width, walls.height, ink);
  const int fontId = walls.height * 4 >= renderer.getLineHeight(UI_10_FONT_ID) * 3 ? UI_10_FONT_ID : SMALL_FONT_ID;
  const char digit[2] = {static_cast<char>('0' + count), '\0'};
  centerText(renderer, fontId, walls, digit, !ink, EpdFontFamily::BOLD);
}

// The board square a token stands on, false while it is in its yard or home.
bool ludoSquare(const Ludo& game, const uint8_t player, const uint8_t token, Ludo::Cell& out) {
  const int8_t shape = game.shapeOf(player);
  const int8_t at = game.position(player, token);
  if (shape < 0 || at < 0 || at >= static_cast<int8_t>(Ludo::HOME)) return false;
  out = at > static_cast<int8_t>(Ludo::LAST_TRACK)
            ? Ludo::homeCell(static_cast<uint8_t>(shape), static_cast<uint8_t>(at - Ludo::TRACK))
            : Ludo::trackCell(Ludo::absoluteCell(static_cast<uint8_t>(shape), at));
  return true;
}

void drawLudo(const GfxRenderer& renderer, const GameSession& session, const Ludo& game, const Rect& content) {
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const int rowHeight = lineHeight + px(8);
  const int gap = px(12);
  // The legend, a row per player and one for the die, goes beside the board
  // when the width allows and under it in two columns otherwise. It is measured
  // before the board is sized, so neither runs past `content`.
  const int sideWidth = px(300);
  const bool sideLegend = content.width - content.height >= sideWidth + gap;
  const int columns = sideLegend ? 1 : 2;
  const int legendHeight = ((game.players() + columns - 1) / columns + 1) * rowHeight;
  const int boardSide = sideLegend ? std::min(content.height, content.width - sideWidth - gap)
                                   : std::min(content.width, content.height - legendHeight - gap);
  const int cell = std::max(0, boardSide) / Ludo::GRID;
  const int boardLeft = content.x + (sideLegend ? 0 : (content.width - cell * Ludo::GRID) / 2);
  const int boardTop = content.y;
  const int bold = std::max(3, cell / 16);

  const auto cellRect = [&](const Ludo::Cell& at) {
    return Rect(boardLeft + at.col * cell, boardTop + at.row * cell, cell, cell);
  };

  // Whose turn it is, or who won; nobody while shapes are being picked.
  const bool picking = game.phase() == Ludo::Phase::Pick;
  const auto marked = [&](const uint8_t player) {
    return game.phase() == Ludo::Phase::Over ? game.winner() == static_cast<int8_t>(player)
                                             : !picking && game.turn() == player;
  };

  // Yards, one 6x6 block per corner, each with its shape as an emblem in the
  // middle 2x2 cells, clear of the four parking cells around it. A taken yard
  // is framed bold and names its owner along the top row, beside a house
  // counting their tokens home; the marked player's row is inverted.
  static constexpr uint8_t YARD_COL[Ludo::SHAPES] = {0, 9, 9, 0};
  static constexpr uint8_t YARD_ROW[Ludo::SHAPES] = {0, 0, 9, 9};
  for (uint8_t shape = 0; shape < Ludo::SHAPES; shape++) {
    const Rect yard(boardLeft + YARD_COL[shape] * cell, boardTop + YARD_ROW[shape] * cell, cell * 6, cell * 6);
    const Rect emblem(yard.x + cell * 2, yard.y + cell * 2, cell * 2, cell * 2);
    const int8_t owner = game.playerWithShape(shape);
    if (owner < 0) {
      renderer.drawRect(yard.x, yard.y, yard.width, yard.height, 1, true);
      drawShape(renderer, shape, emblem, Color::White, 1);
      continue;
    }
    renderer.drawRect(yard.x, yard.y, yard.width, yard.height, bold, true);
    drawShape(renderer, shape, emblem, Color::LightGray, bold);
    const uint8_t player = static_cast<uint8_t>(owner);
    const bool inverted = marked(player);
    if (inverted) renderer.fillRect(yard.x, yard.y, yard.width, cell, true);
    const Rect band(yard.x, yard.y + bold, yard.width, cell - bold);
    const int house = picking ? 0 : band.height - 2 * px(4);
    const int spacing = house > 0 ? px(8) : 0;
    const char* name = session.player(player).name;
    const int room = yard.width - 4 * bold - house - spacing;
    const int fontId =
        renderer.getTextWidth(UI_10_FONT_ID, name, EpdFontFamily::BOLD) <= room ? UI_10_FONT_ID : SMALL_FONT_ID;
    const int nameWidth = renderer.getTextWidth(fontId, name, EpdFontFamily::BOLD);
    const int left = band.x + (band.width - nameWidth - spacing - house) / 2;
    renderer.drawText(fontId, left, band.y + (band.height - renderer.getLineHeight(fontId)) / 2, name, !inverted,
                      EpdFontFamily::BOLD);
    if (house > 0) {
      drawHomeBadge(renderer, Rect(left + nameWidth + spacing, band.y + (band.height - house) / 2, house, house),
                    game.tokensHome(player), !inverted);
    }
  }

  // The shared ring; the squares no one can be captured on are hatched (a mark
  // shaped like anything would read as a player's shape).
  for (uint8_t index = 0; index < Ludo::TRACK; index++) {
    const Rect box = cellRect(Ludo::trackCell(index));
    if (Ludo::isSafeCell(index)) renderer.fillRectDither(box.x, box.y, box.width, box.height, Color::LightGray);
    renderer.drawRect(box.x, box.y, box.width, box.height, 1, true);
  }

  // Home columns, a small outline of their shape in every cell.
  const int homeMark = cell / 2;
  for (uint8_t shape = 0; shape < Ludo::SHAPES; shape++) {
    const int stroke = game.playerWithShape(shape) >= 0 ? std::max(2, cell / 24) : 1;
    for (uint8_t step = 0; step < 5; step++) {
      const Rect box = cellRect(Ludo::homeCell(shape, step));
      renderer.drawRect(box.x, box.y, box.width, box.height, 1, true);
      drawShape(renderer, shape, Rect(box.x + (cell - homeMark) / 2, box.y + (cell - homeMark) / 2, homeMark, homeMark),
                Color::White, stroke);
    }
  }

  // Home, split into a quarter per shape in the yards' corner order; a token
  // that made it parks in its quarter at its own number's slot.
  const Rect home(boardLeft + 6 * cell, boardTop + 6 * cell, cell * 3, cell * 3);
  renderer.drawRect(home.x, home.y, home.width, home.height, bold, true);
  bool anyHome = false;
  for (uint8_t player = 0; player < game.players(); player++) anyHome = anyHome || game.tokensHome(player) > 0;
  if (!anyHome) centerText(renderer, SMALL_FONT_ID, home, "HOME");

  // Tokens last so they sit above the board.
  for (uint8_t player = 0; player < game.players(); player++) {
    const int8_t picked = game.shapeOf(player);
    if (picked < 0) continue;  // still choosing a corner
    const uint8_t shape = static_cast<uint8_t>(picked);
    for (uint8_t token = 0; token < Ludo::TOKENS; token++) {
      const int8_t at = game.position(player, token);
      if (at == Ludo::IN_YARD) {
        drawLudoToken(renderer, shape, token, cellRect(Ludo::yardCell(shape, token)), false);
        continue;
      }
      if (at == static_cast<int8_t>(Ludo::HOME)) {
        const int quarter = home.width / 2;
        const int slot = quarter / 2;
        const int x = home.x + YARD_COL[shape] / 9 * quarter + (token % 2) * slot;
        const int y = home.y + YARD_ROW[shape] / 9 * quarter + (token / 2) * slot;
        drawLudoToken(renderer, shape, token, Rect(x, y, slot, slot), false);
        continue;
      }

      // One token per player per square: the lowest number on top of a pile.
      Ludo::Cell square{};
      ludoSquare(game, player, token, square);
      bool covered = false;
      bool pile = false;
      bool shared = false;
      for (uint8_t p = 0; p < game.players(); p++) {
        for (uint8_t t = 0; t < Ludo::TOKENS; t++) {
          Ludo::Cell other{};
          if ((p == player && t == token) || !ludoSquare(game, p, t, other)) continue;
          if (other.col != square.col || other.row != square.row) continue;
          if (p != player) {
            shared = true;
          } else if (t < token) {
            covered = true;
          } else {
            pile = true;
          }
        }
      }
      if (covered) continue;
      Rect box = cellRect(square);
      if (shared) {
        // Two shapes on one safe square: each takes its own corner of it.
        const int half = cell / 2;
        box = Rect(box.x + YARD_COL[shape] / 9 * half, box.y + YARD_ROW[shape] / 9 * half, half, half);
      }
      drawLudoToken(renderer, shape, token, box, pile);
    }
  }

  // Legend: each player's shape, name and progress; a bar marks whose turn it is.
  const int legendLeft = sideLegend ? boardLeft + cell * Ludo::GRID + gap : content.x;
  const int legendTop = sideLegend ? content.y : boardTop + cell * Ludo::GRID + gap;
  const int columnWidth = (content.x + content.width - legendLeft) / columns;
  const int icon = lineHeight - px(6);
  const int marker = px(4);
  for (uint8_t player = 0; player < game.players(); player++) {
    const int x = legendLeft + (player % columns) * columnWidth;
    const int y = legendTop + (player / columns) * rowHeight;
    const bool onTurn = marked(player);
    if (onTurn) renderer.fillRect(x, y + px(2), marker, rowHeight - px(4), true);

    const int iconX = x + marker + px(8);
    const Rect iconBox(iconX, y + (rowHeight - icon) / 2, icon, icon);
    const int8_t shape = game.shapeOf(player);
    if (shape >= 0) {
      drawShape(renderer, static_cast<uint8_t>(shape), iconBox, Color::White, std::max(2, icon / 12));
    } else {
      centerText(renderer, UI_10_FONT_ID, iconBox, "?", true, EpdFontFamily::BOLD);  // still choosing
    }

    char label[40];
    if (picking) {
      snprintf(label, sizeof(label), "%s", session.player(player).name);
    } else {
      snprintf(label, sizeof(label), "%s  %u home", session.player(player).name, game.tokensHome(player));
    }
    const int textX = iconX + icon + px(10);
    const EpdFontFamily::Style style = onTurn ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
    const int fontId = renderer.getTextWidth(UI_10_FONT_ID, label, style) <= x + columnWidth - textX ? UI_10_FONT_ID
                                                                                                     : SMALL_FONT_ID;
    renderer.drawText(fontId, textX, y + (rowHeight - renderer.getLineHeight(fontId)) / 2, label, true, style);
  }
  if (game.die() > 0) {
    char die[24];
    snprintf(die, sizeof(die), "Die: %u", game.die());
    const int y = legendTop + (game.players() + columns - 1) / columns * rowHeight;
    renderer.drawText(UI_10_FONT_ID, legendLeft + marker + px(8), y + (rowHeight - lineHeight) / 2, die, true,
                      EpdFontFamily::BOLD);
  }
}

// --- picker thumbnails -------------------------------------------------------

// The Games picker's art: one fixed scene per game in the boards' fill
// vocabulary, laid out in the largest square centred in the art rect. Every
// size derives from that square's side so the art scales with the tile.
struct ThumbSquare {
  int x;
  int y;
  int side;
  int thin;  // cell and grid outlines
  int bold;  // frames and marks
};

ThumbSquare thumbSquare(const Rect& art) {
  const int side = std::max(0, std::min(art.width, art.height));
  return ThumbSquare{art.x + (art.width - side) / 2, art.y + (art.height - side) / 2, side, std::max(1, side / 256),
                     std::max(2, side / 100)};
}

// Corner-to-corner X. drawLine stacks a thick line's rows downward, so each
// stroke starts half a width up to stay centred on its end points.
void drawCross(const GfxRenderer& renderer, const Rect& box, const int inset, const int width, const bool black) {
  const int left = box.x + inset;
  const int top = box.y + inset - width / 2;
  const int right = box.x + box.width - 1 - inset;
  const int bottom = box.y + box.height - 1 - inset - width / 2;
  renderer.drawLine(left, top, right, bottom, width, black);
  renderer.drawLine(right, top, left, bottom, width, black);
}

void fillDot(const GfxRenderer& renderer, const int cx, const int cy, const int size) {
  renderer.fillRoundedRect(cx - size / 2, cy - size / 2, size, size, size / 2, Color::Black);
}

// A bold "?" built from arcs so it scales with the art: the top half and
// lower-right quarter of a ring, a stem under it, then a round dot.
void drawQuestionMark(const GfxRenderer& renderer, const Rect& box) {
  const int height = std::min(box.height, box.width * 5 / 3);
  const int radius = height * 3 / 10;
  const int stroke = std::max(2, height / 7);
  const int cx = box.x + box.width / 2;
  const int top = box.y + (box.height - height) / 2;
  const int cy = top + radius;
  renderer.drawArc(radius, cx, cy, -1, -1, stroke, true);
  renderer.drawArc(radius, cx, cy, 1, -1, stroke, true);
  renderer.drawArc(radius, cx, cy, 1, 1, stroke, true);
  const int stemTop = cy + radius - stroke;
  renderer.fillRect(cx - stroke / 2, stemTop, stroke, top + height * 3 / 4 - stemTop, true);
  const int dot = stroke * 5 / 4;
  fillDot(renderer, cx, top + height - dot / 2, dot);
}

// One Undercover player: a round head over a body with rounded shoulders,
// filling `figure` down to its bottom edge. The odd one out is hatched inside
// an ink outline: solid ink first, then the hatch one outline width in
// (fillRectDither writes paper pixels as well as ink).
void drawFigure(const GfxRenderer& renderer, const Rect& figure, const bool odd, const int outline) {
  const int bodyHeight = figure.height * 16 / 25;
  const int neck = figure.height / 25;
  const int head = std::min(figure.width, figure.height - bodyHeight - neck);
  const int shoulder = figure.width * 2 / 5;
  const Rect body(figure.x, figure.y + figure.height - bodyHeight, figure.width, bodyHeight);
  const Rect face(figure.x + (figure.width - head) / 2, body.y - neck - head, head, head);

  renderer.fillRoundedRect(face.x, face.y, face.width, face.height, head / 2, Color::Black);
  renderer.fillRoundedRect(body.x, body.y, body.width, body.height, shoulder, true, true, false, false, Color::Black);
  if (!odd) return;
  renderer.fillRoundedRect(face.x + outline, face.y + outline, head - 2 * outline, head - 2 * outline,
                           head / 2 - outline, Color::LightGray);
  renderer.fillRoundedRect(body.x + outline, body.y + outline, body.width - 2 * outline, body.height - 2 * outline,
                           shoulder - outline, true, true, false, false, Color::LightGray);
  drawQuestionMark(renderer, Rect(body.x + body.width / 5, body.y + body.height / 8, body.width * 3 / 5,
                                  body.height * 3 / 4));
}

void drawUndercoverThumb(const GfxRenderer& renderer, const ThumbSquare& sq) {
  constexpr int FIGURES = 4;
  constexpr int ODD_FIGURE = 2;
  const int width = sq.side / 5;
  const int height = width * 15 / 8;
  const int top = sq.y + (sq.side - height) / 2;
  const int spare = sq.side - FIGURES * width;
  for (int i = 0; i < FIGURES; i++) {
    const int x = sq.x + spare * (i + 1) / (FIGURES + 1) + i * width;
    drawFigure(renderer, Rect(x, top, width, height), i == ODD_FIGURE, sq.bold);
  }
}

// Square, row by row, one char per tile: R red agent, B blue agent, N
// bystander, A assassin, '.' unrevealed.
constexpr const char* CODENAMES_ART[] = {"R.B.N", ".RA.B", "B.R..", ".N.BR", "R..B."};

void drawCodenamesThumb(const GfxRenderer& renderer, const ThumbSquare& sq) {
  constexpr int GRID = sizeof(CODENAMES_ART) / sizeof(CODENAMES_ART[0]);
  const int gap = std::max(2, sq.side / 50);
  const int tile = (sq.side - (GRID - 1) * gap) / GRID;
  const int origin = (sq.side - GRID * tile - (GRID - 1) * gap) / 2;
  const int strikeInset = tile / 6;
  const int crossWidth = std::max(2, tile / 8);  // as Battleship's hits
  for (int row = 0; row < GRID; row++) {
    for (int column = 0; column < GRID; column++) {
      const Rect box(sq.x + origin + column * (tile + gap), sq.y + origin + row * (tile + gap), tile, tile);
      switch (CODENAMES_ART[row][column]) {
        case 'R':
          drawSwatch(renderer, box, 0);
          break;
        case 'B':
          drawSwatch(renderer, box, 2, sq.thin);
          break;
        case 'N':
          renderer.drawRect(box.x, box.y, box.width, box.height, sq.thin, true);
          renderer.fillRect(box.x + strikeInset, box.y + (tile - sq.bold) / 2, tile - 2 * strikeInset, sq.bold, true);
          break;
        case 'A':
          renderer.fillRect(box.x, box.y, box.width, box.height, true);
          drawCross(renderer, box, tile / 5, crossWidth, false);
          break;
        default:
          renderer.drawRect(box.x, box.y, box.width, box.height, sq.thin, true);
          break;
      }
    }
  }
}

// Square, row by row, one char per cell: S ship, X hit, o miss, '.' water.
constexpr const char* BATTLESHIP_ART[] = {"......", ".SSSX.", "....o.", ".o....", "...X.S", ".....S"};

void drawBattleshipThumb(const GfxRenderer& renderer, const ThumbSquare& sq) {
  constexpr int GRID = sizeof(BATTLESHIP_ART) / sizeof(BATTLESHIP_ART[0]);
  const int cell = sq.side / GRID;
  const int size = cell * GRID;
  const int left = sq.x + (sq.side - size) / 2;
  const int top = sq.y + (sq.side - size) / 2;
  for (int i = 1; i < GRID; i++) {
    renderer.fillRect(left + i * cell, top, sq.thin, size, true);
    renderer.fillRect(left, top + i * cell, size, sq.thin, true);
  }

  const int hitWidth = std::max(2, cell / 8);
  const int missDot = std::max(4, cell / 4);
  for (int row = 0; row < GRID; row++) {
    for (int column = 0; column < GRID; column++) {
      const Rect box(left + column * cell, top + row * cell, cell, cell);
      switch (BATTLESHIP_ART[row][column]) {
        case 'S':
          renderer.fillRect(box.x, box.y, box.width, box.height, true);
          break;
        case 'X':
          drawCross(renderer, box, cell / 5, hitWidth, true);
          break;
        case 'o':
          fillDot(renderer, box.x + cell / 2, box.y + cell / 2, missDot);
          break;
        default:
          break;
      }
    }
  }
  renderer.drawRect(left, top, size, size, sq.bold, true);
}

void drawLudoThumb(const GfxRenderer& renderer, const ThumbSquare& sq) {
  const int yard = sq.side * 2 / 5;
  const int farEdge = sq.side - yard;
  // Corners in shape order, as drawLudo's YARD_COL/YARD_ROW.
  static constexpr uint8_t YARD_X[Ludo::SHAPES] = {0, 1, 1, 0};
  static constexpr uint8_t YARD_Y[Ludo::SHAPES] = {0, 0, 1, 1};
  const int nestInset = yard / 5;
  for (uint8_t shape = 0; shape < Ludo::SHAPES; shape++) {
    const Rect box(sq.x + YARD_X[shape] * farEdge, sq.y + YARD_Y[shape] * farEdge, yard, yard);
    drawSwatch(renderer, box, shape, sq.thin);
    // A paper nest keeps the shape readable on its yard's fill.
    const Rect nest(box.x + nestInset, box.y + nestInset, yard - 2 * nestInset, yard - 2 * nestInset);
    renderer.fillRect(nest.x, nest.y, nest.width, nest.height, false);
    renderer.drawRect(nest.x, nest.y, nest.width, nest.height, sq.thin, true);
    const int mark = nest.width * 3 / 4;
    drawShape(renderer, shape, Rect(nest.x + (nest.width - mark) / 2, nest.y + (nest.height - mark) / 2, mark, mark),
              Color::White, sq.bold);
  }

  // A five on the die in the centre square between the yards; pips sit on
  // the face's quarter lines.
  const int margin = sq.side / 50;
  const int die = sq.side - 2 * yard - 2 * margin;
  const Rect face(sq.x + yard + margin, sq.y + yard + margin, die, die);
  renderer.fillRoundedRect(face.x, face.y, die, die, die / 6, Color::White);
  renderer.drawRoundedRect(face.x, face.y, die, die, sq.bold, die / 6, true);
  static constexpr uint8_t PIP_X[5] = {1, 3, 2, 1, 3};
  static constexpr uint8_t PIP_Y[5] = {1, 1, 2, 3, 3};
  const int pip = std::max(3, die / 6);
  for (uint8_t i = 0; i < 5; i++) {
    fillDot(renderer, face.x + die * PIP_X[i] / 4, face.y + die * PIP_Y[i] / 4, pip);
  }
  renderer.drawRect(sq.x, sq.y, sq.side, sq.side, sq.bold, true);
}

}  // namespace

const char* titleFor(const party::GameId id) {
  switch (id) {
    case party::GameId::Undercover:
      return tr(STR_GAME_UNDERCOVER);
    case party::GameId::Codenames:
      return tr(STR_GAME_CODENAMES);
    case party::GameId::Battleship:
      return tr(STR_GAME_BATTLESHIP);
    case party::GameId::Ludo:
      return tr(STR_GAME_LUDO);
    default:
      return "";
  }
}

void drawThumbnail(const GfxRenderer& renderer, const party::GameId id, const Rect& art) {
  const ThumbSquare sq = thumbSquare(art);
  if (sq.side < 20) return;
  switch (id) {
    case party::GameId::Undercover:
      drawUndercoverThumb(renderer, sq);
      return;
    case party::GameId::Codenames:
      drawCodenamesThumb(renderer, sq);
      return;
    case party::GameId::Battleship:
      drawBattleshipThumb(renderer, sq);
      return;
    case party::GameId::Ludo:
      drawLudoThumb(renderer, sq);
      return;
    default:
      return;
  }
}

void statusFor(const GameSession& session, char* out, const size_t cap) {
  if (!out || cap == 0) return;
  const party::Game* game = session.game();
  if (!game) {
    const party::GameMeta& meta = party::gameMeta(session.selected());
    if (session.playerCount() < meta.minPlayers) {
      snprintf(out, cap, tr(STR_GAMES_NEEDS_PLAYERS_FORMAT), titleFor(session.selected()), meta.minPlayers,
               session.playerCount());
    } else {
      snprintf(out, cap, tr(STR_GAMES_READY_FORMAT), titleFor(session.selected()), session.playerCount());
    }
    return;
  }

  switch (game->id()) {
    case party::GameId::Undercover:
      undercoverStatus(session, *static_cast<const Undercover*>(game), out, cap);
      return;
    case party::GameId::Codenames:
      codenamesStatus(*static_cast<const Codenames*>(game), out, cap);
      return;
    case party::GameId::Battleship:
      battleshipStatus(session, *static_cast<const Battleship*>(game), out, cap);
      return;
    case party::GameId::Ludo:
      ludoStatus(session, *static_cast<const Ludo*>(game), out, cap);
      return;
    default:
      out[0] = '\0';
      return;
  }
}

void drawSeatList(const GfxRenderer& renderer, const GameSession& session, const Rect& content, const uint32_t nowMs) {
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const int rowHeight = lineHeight + px(8);
  int y = content.y;

  if (session.playerCount() == 0) {
    renderer.drawText(UI_10_FONT_ID, content.x, y, tr(STR_GAMES_NO_PLAYERS), true, EpdFontFamily::ITALIC);
    return;
  }

  for (uint8_t seat = 0; seat < party::MAX_PLAYERS; seat++) {
    if (!session.isSeated(seat)) continue;
    if (y + rowHeight > content.y + content.height) break;

    // Seat, name (MAX_NAME_LEN), then up to three tags; the test tag is translated.
    char label[64];
    const bool test = session.isTestSeat(seat);
    snprintf(label, sizeof(label), "%u  %s%s%s%s%s", seat + 1, session.player(seat).name,
             session.isHost(seat) ? "  (host)" : "", session.isAway(seat, nowMs) ? "  (away)" : "", test ? "  " : "",
             test ? tr(STR_GAMES_TEST_TAG) : "");
    renderer.drawText(UI_10_FONT_ID, content.x, y, label);
    y += rowHeight;
  }
}

void drawBoard(const GfxRenderer& renderer, const GameSession& session, const Rect& content) {
  const party::Game* game = session.game();
  if (!game) return;

  switch (game->id()) {
    case party::GameId::Undercover:
      drawUndercover(renderer, session, *static_cast<const Undercover*>(game), content);
      return;
    case party::GameId::Codenames:
      drawCodenames(renderer, *static_cast<const Codenames*>(game), content);
      return;
    case party::GameId::Battleship:
      drawBattleship(renderer, session, *static_cast<const Battleship*>(game), content);
      return;
    case party::GameId::Ludo:
      drawLudo(renderer, session, *static_cast<const Ludo*>(game), content);
      return;
    default:
      return;
  }
}

}  // namespace games
