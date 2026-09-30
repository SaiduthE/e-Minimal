#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <string>

#include "DashboardTiles.h"
#include "DateNames.h"
#include "components/UITheme.h"

namespace dashboard_tiles {
namespace {

bool isLeapYear(const int year) { return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0; }

int daysInMonth(const int year, const int mon) {
  static constexpr int DAYS[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  return mon == 1 && isLeapYear(year) ? 29 : DAYS[mon];
}

// 0 = Sunday, for a Gregorian date with mon 0-11 (Sakamoto's method).
int weekdayOf(int year, const int mon, const int day) {
  static constexpr int OFFSETS[12] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  if (mon < 2) year--;
  return (year + year / 4 - year / 100 + year / 400 + OFFSETS[mon] + day) % 7;
}

const char* weekdayInitial(const int wday) {
  static constexpr StrId NAMES[7] = {StrId::STR_SUN_INITIAL, StrId::STR_MON_INITIAL, StrId::STR_TUE_INITIAL,
                                     StrId::STR_WED_INITIAL, StrId::STR_THU_INITIAL, StrId::STR_FRI_INITIAL,
                                     StrId::STR_SAT_INITIAL};
  return I18N.get(NAMES[((wday % 7) + 7) % 7]);
}

// Centred on centerX, cut to maxWidth.
void drawFitted(const GfxRenderer& renderer, const int fontId, const int centerX, const int y, const int maxWidth,
                const char* text, const bool bold = false) {
  const auto style = bold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
  if (renderer.getTextWidth(fontId, text, style) <= maxWidth) {
    ui::drawCentered(renderer, fontId, centerX, y, text, bold);
    return;
  }
  const std::string fitted = renderer.truncatedText(fontId, text, maxWidth, style);
  ui::drawCentered(renderer, fontId, centerX, y, fitted.c_str(), bold);
}

// Weekday, big day number, then "September 2026" (or the month and the year
// on lines of their own when that is too wide), centred over one another.
struct Stack {
  const char* weekday = nullptr;
  char day[4] = "";
  char month[40] = "";
  char year[8] = "";
  int monthFont = UI_12_FONT_ID;
  int gap = 0;
  int height = 0;
};

int stackHeight(const GfxRenderer& renderer, const Stack& stack) {
  const int monthLine = renderer.getLineHeight(stack.monthFont) + stack.gap;
  int height = renderer.getLineHeight(ui::BIG_FONT_ID);
  if (stack.weekday) height += renderer.getLineHeight(UI_12_FONT_ID) + stack.gap;
  if (stack.month[0]) height += monthLine;
  if (stack.year[0]) height += monthLine;
  return height;
}

// A small tile takes the short weekday and a smaller month; the month line
// splits, then steps down a face, to fit the width. Lines that don't fit the
// height go from the bottom up.
void planStack(const GfxRenderer& renderer, const Rect& area, const tm& local, const bool small, Stack& stack) {
  stack.gap = UITheme::scaledPx(small ? 2 : 8);
  stack.weekday = date_names::weekday(local.tm_wday);
  if (small || renderer.getTextWidth(UI_12_FONT_ID, stack.weekday, EpdFontFamily::BOLD) > area.width) {
    stack.weekday = date_names::weekdayShort(local.tm_wday);
  }
  snprintf(stack.day, sizeof(stack.day), "%d", local.tm_mday);
  const int year = local.tm_year + 1900;
  const char* month = date_names::month(local.tm_mon);
  const int fonts[2] = {small ? UI_10_FONT_ID : UI_12_FONT_ID, small ? SMALL_FONT_ID : UI_10_FONT_ID};
  for (const int fontId : fonts) {
    stack.monthFont = fontId;
    stack.year[0] = '\0';
    snprintf(stack.month, sizeof(stack.month), tr(STR_DASH_MONTH_YEAR_FORMAT), month, year);
    if (renderer.getTextWidth(fontId, stack.month) <= area.width) break;
    snprintf(stack.month, sizeof(stack.month), "%s", month);
    snprintf(stack.year, sizeof(stack.year), "%d", year);
    if (renderer.getTextWidth(fontId, stack.month) <= area.width) break;
  }
  if (stackHeight(renderer, stack) > area.height) stack.year[0] = '\0';
  if (stackHeight(renderer, stack) > area.height) stack.month[0] = '\0';
  if (stackHeight(renderer, stack) > area.height) stack.weekday = nullptr;
  stack.height = stackHeight(renderer, stack);
}

void drawStack(const GfxRenderer& renderer, const Stack& stack, const Rect& area, int y) {
  const int centerX = area.x + area.width / 2;
  if (stack.weekday) {
    drawFitted(renderer, UI_12_FONT_ID, centerX, y, area.width, stack.weekday, true);
    y += renderer.getLineHeight(UI_12_FONT_ID) + stack.gap;
  }
  ui::drawCentered(renderer, ui::BIG_FONT_ID, centerX, y, stack.day, true);
  y += renderer.getLineHeight(ui::BIG_FONT_ID) + stack.gap;
  if (stack.month[0]) {
    drawFitted(renderer, stack.monthFont, centerX, y, area.width, stack.month);
    y += renderer.getLineHeight(stack.monthFont) + stack.gap;
  }
  if (stack.year[0]) drawFitted(renderer, stack.monthFont, centerX, y, area.width, stack.year);
}

// "Tuesday, 29 September" on one line in the largest face that fits, and the
// year smaller: under it, or beside it on a strip (dropped if no room).
struct DateLine {
  char text[64] = "";
  char year[8] = "";
  int fontId = ui::BIG_FONT_ID;
  int yearFont = UI_12_FONT_ID;
  int width = 0;
  bool beside = false;
  int height = 0;
};

int yearGap() { return UITheme::scaledPx(4); }
int besideGap() { return UITheme::scaledPx(16); }

// False when no face fits the line; the tile then stacks the date instead.
bool planLine(const GfxRenderer& renderer, const int maxWidth, const tm& local, const bool yearUnder, DateLine& line) {
  static constexpr int FONTS[2] = {ui::BIG_FONT_ID, UI_12_FONT_ID};
  const char* month = date_names::month(local.tm_mon);
  // The big face with the full, then the short weekday; then the same in UI_12.
  bool fits = false;
  for (int i = 0; i < 4 && !fits; i++) {
    const char* weekday = i % 2 == 0 ? date_names::weekday(local.tm_wday) : date_names::weekdayShort(local.tm_wday);
    line.fontId = FONTS[i / 2];
    snprintf(line.text, sizeof(line.text), tr(STR_DASH_DATE_FORMAT), weekday, local.tm_mday, month);
    line.width = renderer.getTextWidth(line.fontId, line.text, EpdFontFamily::BOLD);
    fits = line.width <= maxWidth;
  }
  snprintf(line.year, sizeof(line.year), "%d", local.tm_year + 1900);
  line.height = renderer.getLineHeight(line.fontId);
  if (yearUnder) {
    line.yearFont = line.fontId == ui::BIG_FONT_ID ? UI_12_FONT_ID : UI_10_FONT_ID;
    line.height += yearGap() + renderer.getLineHeight(line.yearFont);
    return fits;
  }
  line.yearFont = UI_10_FONT_ID;
  line.beside = line.width + besideGap() + renderer.getTextWidth(line.yearFont, line.year) <= maxWidth;
  if (!line.beside) line.year[0] = '\0';
  return fits;
}

void drawDateLine(const GfxRenderer& renderer, const DateLine& line, const Rect& area, const int y) {
  const int centerX = area.x + area.width / 2;
  if (line.beside) {
    const int total = line.width + besideGap() + renderer.getTextWidth(line.yearFont, line.year);
    const int x = centerX - total / 2;
    renderer.drawText(line.fontId, x, y, line.text, true, EpdFontFamily::BOLD);
    // On the big text's baseline.
    const int drop = renderer.getFontAscenderSize(line.fontId) - renderer.getFontAscenderSize(line.yearFont);
    renderer.drawText(line.yearFont, x + line.width + besideGap(), y + drop, line.year);
    return;
  }
  drawFitted(renderer, line.fontId, centerX, y, area.width, line.text, true);
  if (line.year[0]) {
    const int yearY = y + renderer.getLineHeight(line.fontId) + yearGap();
    drawFitted(renderer, line.yearFont, centerX, yearY, area.width, line.year);
  }
}

// The month as a Monday-first grid under a row of weekday initials.
struct Calendar {
  int lead = 0;  // blank cells before the 1st
  int days = 0;
  int rows = 0;  // the initials plus the weeks
  int cellWidth = 0;
  int cellHeight = 0;
  int fontId = UI_10_FONT_ID;
};

// Picks the cell size and the largest face that fits it; false when even the smallest won't.
bool planCalendar(const GfxRenderer& renderer, const int width, const int height, const tm& local, Calendar& calendar) {
  const int year = local.tm_year + 1900;
  const int mon = ((local.tm_mon % 12) + 12) % 12;
  calendar.days = daysInMonth(year, mon);
  calendar.lead = (weekdayOf(year, mon, 1) + 6) % 7;
  calendar.rows = 1 + (calendar.lead + calendar.days + 6) / 7;
  if (height <= 0 || width <= 0) return false;
  calendar.cellHeight = std::min(height / calendar.rows, UITheme::scaledPx(80));
  calendar.cellWidth = std::min(width / 7, calendar.cellHeight * 3 / 2);
  calendar.cellHeight = std::min(calendar.cellHeight, calendar.cellWidth);
  static constexpr int FONTS[3] = {UI_12_FONT_ID, UI_10_FONT_ID, SMALL_FONT_ID};
  for (const int fontId : FONTS) {
    calendar.fontId = fontId;
    if (calendar.cellHeight >= renderer.getLineHeight(fontId) + UITheme::scaledPx(6)) return true;
  }
  return calendar.cellHeight >= renderer.getLineHeight(SMALL_FONT_ID);
}

void drawCalendar(const GfxRenderer& renderer, const Calendar& calendar, const int x, const int y, const int today) {
  const int cellWidth = calendar.cellWidth;
  const int cellHeight = calendar.cellHeight;
  const int textTop = (cellHeight - renderer.getLineHeight(calendar.fontId)) / 2;
  for (int column = 0; column < 7; column++) {
    drawFitted(renderer, calendar.fontId, x + column * cellWidth + cellWidth / 2, y + textTop, cellWidth,
               weekdayInitial((column + 1) % 7), true);
  }
  renderer.drawLine(x, y + cellHeight - 2, x + cellWidth * 7 - 1, y + cellHeight - 2, 2, true);

  // Today is a filled box with white figures.
  const int inset = UITheme::scaledPx(3);
  char number[4];
  for (int day = 1; day <= calendar.days; day++) {
    const int index = calendar.lead + day - 1;
    const int cellX = x + (index % 7) * cellWidth;
    const int cellY = y + (1 + index / 7) * cellHeight;
    const bool isToday = day == today;
    if (isToday) {
      renderer.fillRoundedRect(cellX + inset, cellY + inset, cellWidth - inset * 2, cellHeight - inset * 2,
                               UITheme::scaledPx(8), Color::Black);
    }
    snprintf(number, sizeof(number), "%d", day);
    const auto style = isToday ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
    const int textX = cellX + (cellWidth - renderer.getTextWidth(calendar.fontId, number, style)) / 2;
    renderer.drawText(calendar.fontId, textX, cellY + textTop, number, !isToday, style);
  }
}

}  // namespace

void drawDate(GfxRenderer& renderer, const Rect& area, const WeatherService& weather) {
  tm local{};
  if (!localNow(weather, local)) {
    ui::drawNote(renderer, area, area.y, tr(STR_DASH_NO_DATE));
    return;
  }
  const TileSize size = sizeOf(area);
  const bool small = size.scale == TileSize::Scale::Small;

  // A wide tile reads the date as one line when it fits; the others stack it.
  DateLine line;
  Stack stack;
  const bool wide = size.shape == TileSize::Shape::Wide && planLine(renderer, area.width, local, !small, line);
  if (!wide) planStack(renderer, area, local, small, stack);
  const int upper = wide ? line.height : stack.height;

  // A large tile adds the month under it; the whole is centred.
  Calendar calendar;
  const int gap = UITheme::scaledPx(24);
  const bool withCalendar = size.scale == TileSize::Scale::Large &&
                            planCalendar(renderer, area.width, area.height - upper - gap, local, calendar);
  const int total = upper + (withCalendar ? gap + calendar.rows * calendar.cellHeight : 0);
  const int top = area.y + std::max(0, (area.height - total) / 2);
  if (wide) {
    drawDateLine(renderer, line, area, top);
  } else {
    drawStack(renderer, stack, area, top);
  }
  if (withCalendar) {
    const int x = area.x + (area.width - calendar.cellWidth * 7) / 2;
    drawCalendar(renderer, calendar, x, top + upper + gap, local.tm_mday);
  }
}

}  // namespace dashboard_tiles
