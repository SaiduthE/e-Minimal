#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <string>

#include "DashboardTiles.h"
#include "components/UITheme.h"

namespace dashboard_tiles {
namespace {

// A list row's face and spacing for one tile scale. UI_12 is the largest
// uncompressed face (NotoSerif 24 is about its size), so Large gets air, not size.
struct ListStyle {
  int fontId;
  int rowGap;
  int stroke;  // checkbox outline
};

ListStyle listStyleFor(const TileSize::Scale scale) {
  switch (scale) {
    case TileSize::Scale::Small:
      return {UI_10_FONT_ID, UITheme::scaledPx(4), 2};
    case TileSize::Scale::Large:
      return {UI_12_FONT_ID, UITheme::scaledPx(18), 3};
    default:
      return {UI_12_FONT_ID, UITheme::scaledPx(8), 2};
  }
}

int columnGap() { return UITheme::scaledPx(24); }

// Wide tiles take 2 columns, 3 from 3:1, while each stays readable.
int columnsFor(const Rect& area, const TileSize size) {
  if (size.shape != TileSize::Shape::Wide) return 1;
  const int minColumn = UITheme::scaledPx(size.scale == TileSize::Scale::Small ? 180 : 240);
  int columns = area.width >= area.height * 3 ? 3 : 2;
  while (columns > 1 && area.width < columns * minColumn + (columns - 1) * columnGap()) columns--;
  return columns;
}

// The title row with as much of the count as fits: open and done, open, the
// bare number, or nothing rather than a cut title.
int drawHeader(const GfxRenderer& renderer, const Rect& area, const TodoSnapshot& todo) {
  const char* title = tr(STR_TODO);
  const int titleWidth = renderer.getTextWidth(UI_12_FONT_ID, title, EpdFontFamily::BOLD);
  const int room = area.width - titleWidth - UITheme::scaledPx(16);
  const auto open = static_cast<unsigned>(todo.open);
  char note[48] = "";
  if (todo.open > 0 && todo.done > 0) {
    snprintf(note, sizeof(note), tr(STR_TODO_SUMMARY_FORMAT), open, static_cast<unsigned>(todo.done));
  }
  if (!note[0] || renderer.getTextWidth(SMALL_FONT_ID, note) > room) {
    snprintf(note, sizeof(note), tr(STR_DASH_TODO_OPEN_FORMAT), open);
  }
  if (renderer.getTextWidth(SMALL_FONT_ID, note) > room) snprintf(note, sizeof(note), "%u", open);
  if (renderer.getTextWidth(SMALL_FONT_ID, note) > room) note[0] = '\0';
  return ui::drawTitle(renderer, area, title, note);
}

// A tile too small for rows: the open count, big, between the title and "open".
void drawCount(const GfxRenderer& renderer, const Rect& area, const TodoSnapshot& todo) {
  const int titleLine = renderer.getLineHeight(UI_12_FONT_ID);
  const int bigLine = renderer.getLineHeight(ui::BIG_FONT_ID);
  const int labelLine = renderer.getLineHeight(UI_10_FONT_ID);
  const int centerX = area.x + area.width / 2;
  const std::string title = renderer.truncatedText(UI_12_FONT_ID, tr(STR_TODO), area.width, EpdFontFamily::BOLD);
  if (todo.open == 0) {
    ui::drawCentered(renderer, UI_12_FONT_ID, centerX, area.y, title.c_str(), true);
    ui::drawNote(renderer, area, area.y + titleLine, tr(STR_TODO_EMPTY));
    return;
  }
  int y = area.y + std::max(0, (area.height - titleLine - bigLine - labelLine) / 2);
  ui::drawCentered(renderer, UI_12_FONT_ID, centerX, y, title.c_str(), true);
  y += titleLine;
  char count[8];
  snprintf(count, sizeof(count), "%u", static_cast<unsigned>(todo.open));
  ui::drawCentered(renderer, ui::BIG_FONT_ID, centerX, y, count, true);
  y += bigLine;
  if (y + labelLine > area.y + area.height) return;
  const std::string label = renderer.truncatedText(UI_10_FONT_ID, tr(STR_DASH_TODO_OPEN_LABEL), area.width);
  ui::drawCentered(renderer, UI_10_FONT_ID, centerX, y, label.c_str());
}

// Checkbox rows filled column by column, with "+N more" in the last slot when
// tasks are left over. Columns are balanced, so few tasks don't crowd the first.
void drawList(const GfxRenderer& renderer, const Rect& box, const TodoSnapshot& todo, const ListStyle& style,
              const int columns) {
  const int line = renderer.getLineHeight(style.fontId);
  const int pitch = line + style.rowGap;
  const int rowsPerColumn = (box.height + style.rowGap) / pitch;
  if (rowsPerColumn <= 0 || todo.shown == 0) return;
  const int columnWidth = (box.width - columnGap() * (columns - 1)) / columns;
  const int capacity = rowsPerColumn * columns;

  int tasks = todo.shown;
  bool more = todo.open > todo.shown;
  if (tasks + (more ? 1 : 0) > capacity) {
    // One slot: the first task beats "+N more", as the title has the count.
    more = capacity > 1;
    tasks = more ? capacity - 1 : 1;
  }
  const int slots = tasks + (more ? 1 : 0);
  const int rowsUsed = (slots + columns - 1) / columns;

  const int check = line / 2;
  const int indent = check + line * 2 / 5;
  const int textWidth = columnWidth - indent;
  const int smallLine = renderer.getLineHeight(SMALL_FONT_ID);
  for (int i = 0; i < slots; i++) {
    const int x = box.x + (i / rowsUsed) * (columnWidth + columnGap());
    const int y = box.y + (i % rowsUsed) * pitch;
    if (i < tasks) {
      renderer.drawRect(x, y + (line - check) / 2, check, check, style.stroke, true);
      const std::string fitted = renderer.truncatedText(style.fontId, todo.tasks[i], textWidth);
      renderer.drawText(style.fontId, x + indent, y, fitted.c_str());
    } else {
      char note[32];
      snprintf(note, sizeof(note), tr(STR_DASH_MORE_FORMAT), static_cast<unsigned>(todo.open - tasks));
      const std::string fitted = renderer.truncatedText(SMALL_FONT_ID, note, textWidth);
      renderer.drawText(SMALL_FONT_ID, x + indent, y + (line - smallLine) / 2, fitted.c_str());
    }
  }
}

}  // namespace

void drawTodo(GfxRenderer& renderer, const Rect& area, const TodoSnapshot& todo) {
  // The 2x2 tile has no room for readable rows.
  const int countOnly = UITheme::scaledPx(180);
  if (area.width < countOnly && area.height < countOnly) {
    drawCount(renderer, area, todo);
    return;
  }
  const TileSize size = sizeOf(area);
  const int top = drawHeader(renderer, area, todo);
  if (todo.open == 0) {
    ui::drawNote(renderer, area, top, tr(STR_TODO_EMPTY));
    return;
  }
  const Rect list{area.x, top, area.width, area.y + area.height - top};
  drawList(renderer, list, todo, listStyleFor(size.scale), columnsFor(area, size));
}

}  // namespace dashboard_tiles
