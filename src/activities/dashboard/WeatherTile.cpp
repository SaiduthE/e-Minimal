#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <iterator>
#include <string>

#include "DashboardTiles.h"
#include "DateNames.h"
#include "WeatherIcons.h"
#include "components/UITheme.h"

// The weather tile, laid out by its tile's class (TileSize). Small: the
// conditions now. Square Medium: now and a few details. Wide: now on the left,
// the next days as columns. Tall: now on top, the next days as a list. Large:
// now, every detail and the next days. Each part is planned against the room
// it has (face, icon size, how many days) before anything is drawn.
namespace dashboard_tiles {
namespace {

using Status = WeatherService::Status;

// The next days' faces, largest first. The UI faces, not NOTOSANS_*: those
// are not registered on e-Minimal, and UI_12 is the larger face there anyway.
constexpr int DAY_FONTS[] = {UI_12_FONT_ID, UI_10_FONT_ID, SMALL_FONT_ID};
constexpr int DAY_FONT_COUNT = static_cast<int>(std::size(DAY_FONTS));
// Large's details, larger first: a step under its UI_12 condition.
constexpr int DETAIL_FONTS[] = {UI_10_FONT_ID, SMALL_FONT_ID};
// The condition and high/low under a compact temperature.
constexpr int COMPACT_FONT = UI_10_FONT_ID;

enum class Detail : uint8_t { Feels, Humidity, Wind, Rain, Sunrise, Sunset };
struct DetailSet {
  const Detail* items;
  int count;
};
constexpr Detail MEDIUM_ITEMS[] = {Detail::Feels, Detail::Rain, Detail::Wind};
constexpr Detail LARGE_ITEMS[] = {
    Detail::Feels, Detail::Humidity, Detail::Wind, Detail::Rain, Detail::Sunrise, Detail::Sunset};
constexpr DetailSet MEDIUM_DETAILS{MEDIUM_ITEMS, static_cast<int>(std::size(MEDIUM_ITEMS))};
constexpr DetailSet LARGE_DETAILS{LARGE_ITEMS, static_cast<int>(std::size(LARGE_ITEMS))};

enum class Align : uint8_t { Left, Center, Right };
enum class DayLayout : uint8_t { Best, Columns, List };

struct Ctx {
  const GfxRenderer& renderer;
  const WeatherService& weather;
  const WeatherForecast& forecast;
  int nextDays;    // forecast days after today
  int gap;         // between blocks side by side
  int rowGap;      // between lines and rows
  int sectionGap;  // between stacked blocks, and each side of a rule

  int line(const int font) const { return renderer.getLineHeight(font); }
  int width(const int font, const char* text, const bool bold = false) const {
    return renderer.getTextWidth(font, text, bold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
  }
};

// Text cut to maxWidth with an ellipsis, placed by align about x.
void drawFitted(const Ctx& c, const int font, const int x, const int y, const char* text, const int maxWidth,
                const Align align = Align::Left, const bool bold = false) {
  if (maxWidth <= 0 || !text || !text[0]) return;
  const auto style = bold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
  int width = c.renderer.getTextWidth(font, text, style);
  std::string fitted;
  if (width > maxWidth) {
    fitted = c.renderer.truncatedText(font, text, maxWidth, style);
    text = fitted.c_str();
    width = c.renderer.getTextWidth(font, text, style);
    if (width > maxWidth) return;  // not even the ellipsis fits
  }
  const int left = align == Align::Left ? x : align == Align::Center ? x - width / 2 : x - width;
  c.renderer.drawText(font, left, y, text, true, style);
}

void formatTemperature(char* out, const size_t size, const float value) {
  snprintf(out, size, "%d\xC2\xB0", ui::rounded(value));
}

void formatPair(char* out, const size_t size, const WeatherForecast::Day& day) {
  snprintf(out, size, tr(STR_DASH_TEMP_PAIR_FORMAT), ui::rounded(day.high), ui::rounded(day.low));
}

void formatHighLow(char* out, const size_t size, const WeatherForecast::Day& day) {
  snprintf(out, size, tr(STR_DASH_HIGH_LOW_FORMAT), ui::rounded(day.high), ui::rounded(day.low));
}

bool formatRain(char* out, const size_t size, const WeatherForecast::Day& day) {
  if (day.precipitationChance < 0) return false;
  snprintf(out, size, tr(STR_DASH_RAIN_FORMAT), static_cast<int>(day.precipitationChance));
  return true;
}

const char* weekdayOf(const Ctx& c, const WeatherForecast::Day& day) {
  return date_names::weekdayShort(weather::localAt(day.date, c.forecast.utcOffset).tm_wday);
}

const char* conditionOf(const Ctx& c) { return I18N.get(weather::conditionFor(c.forecast.code)); }

const uint8_t* iconBitmap(const int16_t code, const int size) {
  return size == ui::HERO_ICON ? weather_icons::hero(code) : weather_icons::small(code);
}

// False when the forecast lacks it.
bool formatDetail(const Ctx& c, const Detail detail, char* out, const size_t size) {
  const auto& forecast = c.forecast;
  const WeatherForecast::Day* today = forecast.dayCount > 0 ? &forecast.days[0] : nullptr;
  switch (detail) {
    case Detail::Feels:
      snprintf(out, size, tr(STR_DASH_FEELS_FORMAT), ui::rounded(forecast.feelsLike));
      return true;
    case Detail::Humidity:
      if (forecast.humidity < 0) return false;
      snprintf(out, size, tr(STR_DASH_HUMIDITY_FORMAT), static_cast<int>(forecast.humidity));
      return true;
    case Detail::Wind:
      snprintf(out, size, tr(STR_DASH_WIND_FORMAT), ui::rounded(forecast.windSpeed),
               c.weather.fahrenheit() ? tr(STR_UNIT_MPH) : tr(STR_UNIT_KMH));
      return true;
    case Detail::Rain:
      if (!today || today->precipitationChance < 0) return false;
      snprintf(out, size, tr(STR_DASH_RAIN_FORMAT), static_cast<int>(today->precipitationChance));
      return true;
    case Detail::Sunrise:
    case Detail::Sunset: {
      if (!today || today->sunrise == 0 || today->sunset == 0) return false;
      const bool rise = detail == Detail::Sunrise;
      const tm local = weather::localAt(rise ? today->sunrise : today->sunset, forecast.utcOffset);
      snprintf(out, size, rise ? tr(STR_DASH_SUNRISE_FORMAT) : tr(STR_DASH_SUNSET_FORMAT), local.tm_hour,
               local.tm_min);
      return true;
    }
  }
  return false;
}

const char* statusNote(const WeatherService& weather, char* buffer, const size_t size) {
  switch (weather.status()) {
    case Status::Updating:
      return tr(STR_DASH_UPDATING);
    case Status::Failed:
      return tr(STR_DASH_UPDATE_FAILED);
    case Status::NoWifi:
      return tr(STR_DASH_NO_WIFI_SHORT);
    default:
      break;
  }
  const auto& forecast = weather.forecast();
  if (!forecast.valid) return "";
  const tm observed = weather::localAt(forecast.observedAt, forecast.utcOffset);
  snprintf(buffer, size, tr(STR_DASH_UPDATED_FORMAT), observed.tm_hour, observed.tm_min);
  return buffer;
}

// drawTitle's row: a UI_12 line, the rule and the gap under it.
int titleHeight(const Ctx& c) { return c.line(UI_12_FONT_ID) + UITheme::scaledPx(20); }

// The place, with the status on the right when both fit. When they don't, the
// update time goes; a problem with the update takes the place's spot instead.
int drawHeader(const Ctx& c, const Rect& area) {
  char note[48];
  const char* status = statusNote(c.weather, note, sizeof(note));
  const char* title = c.weather.place().empty() ? tr(STR_DASH_WEATHER) : c.weather.place().c_str();
  if (status[0]) {
    const int noteWidth = c.width(SMALL_FONT_ID, status) + UITheme::scaledPx(16);
    const int titleWidth = std::min(c.width(UI_12_FONT_ID, title, true), area.width / 2);
    if (noteWidth > area.width - titleWidth) {
      const auto state = c.weather.status();
      const bool problem = state == Status::Updating || state == Status::Failed || state == Status::NoWifi;
      if (problem && c.forecast.valid) title = status;
      status = "";
    }
  }
  return ui::drawTitle(c.renderer, area, title, status);
}

// No place, or no forecast yet: a message, under the title when it fits.
void drawState(const Ctx& c, const Rect& area) {
  const auto status = c.weather.status();
  const char* message = tr(STR_DASH_UPDATING);
  if (!c.weather.hasLocation()) {
    message = tr(STR_DASH_WEATHER_NO_PLACE);
  } else if (status == Status::NoWifi) {
    message = tr(STR_DASH_WEATHER_NO_WIFI);
  } else if (status == Status::Failed) {
    message = tr(STR_DASH_WEATHER_FAILED);
  }
  // drawNote wraps to at most four lines.
  const int lines = c.width(UI_10_FONT_ID, message) <= area.width ? 1 : 4;
  const bool titled = area.height - titleHeight(c) >= lines * c.line(UI_10_FONT_ID);
  ui::drawNote(c.renderer, area, titled ? drawHeader(c, area) : area.y, message);
}

// ---- Now ----

// The icon, with the temperature, condition and today's high/low beside it.
struct Hero {
  int icon = 0;
  int font = 0;  // condition and high/low
  bool condition = false;
  bool highLow = false;
  bool longHighLow = false;  // "High 23 / Low 15", else "23 / 15"
  int textWidth = 0;         // room for the text beside the icon
  int width = 0;
  int height = 0;
};

Hero planHero(const Ctx& c, const int maxWidth, const int maxHeight, const int font) {
  Hero hero;
  hero.font = font;
  char text[48];
  formatTemperature(text, sizeof(text), c.forecast.temperature);
  const int tempWidth = c.width(ui::BIG_FONT_ID, text, true);
  const int conditionWidth = c.width(font, conditionOf(c), true);
  int longWidth = 0;
  int shortWidth = 0;
  if (c.forecast.dayCount > 0) {
    formatHighLow(text, sizeof(text), c.forecast.days[0]);
    longWidth = c.width(font, text);
    formatPair(text, sizeof(text), c.forecast.days[0]);
    shortWidth = c.width(font, text);
  }
  // The big icon only when the text still fits beside it uncut.
  const int needed = std::max({tempWidth, conditionWidth, shortWidth});
  const bool big = maxHeight >= ui::HERO_ICON && maxWidth - ui::HERO_ICON - c.gap >= needed;
  hero.icon = big ? ui::HERO_ICON : ui::SMALL_ICON;
  hero.textWidth = maxWidth - hero.icon - c.gap;

  const int line = c.line(font);
  int textHeight = c.line(ui::BIG_FONT_ID);
  int textWidth = tempWidth;
  hero.condition = textHeight + line <= maxHeight;
  if (hero.condition) {
    textHeight += line;
    textWidth = std::max(textWidth, conditionWidth);
  }
  hero.highLow = hero.condition && c.forecast.dayCount > 0 && textHeight + line <= maxHeight;
  if (hero.highLow) {
    textHeight += line;
    hero.longHighLow = longWidth <= hero.textWidth;
    textWidth = std::max(textWidth, hero.longHighLow ? longWidth : shortWidth);
  }
  hero.width = hero.icon + c.gap + std::min(textWidth, hero.textWidth);
  hero.height = std::max(hero.icon, textHeight);
  return hero;
}

void drawHero(const Ctx& c, const Hero& hero, const int x, const int y) {
  char text[48];
  const int bigLine = c.line(ui::BIG_FONT_ID);
  const int line = c.line(hero.font);
  const int textHeight = bigLine + (hero.condition ? line : 0) + (hero.highLow ? line : 0);
  c.renderer.drawIcon(iconBitmap(c.forecast.code, hero.icon), x, y + (hero.height - hero.icon) / 2, hero.icon);

  const int textX = x + hero.icon + c.gap;
  int textY = y + (hero.height - textHeight) / 2;
  formatTemperature(text, sizeof(text), c.forecast.temperature);
  drawFitted(c, ui::BIG_FONT_ID, textX, textY, text, hero.textWidth, Align::Left, true);
  textY += bigLine;
  if (hero.condition) {
    drawFitted(c, hero.font, textX, textY, conditionOf(c), hero.textWidth, Align::Left, true);
    textY += line;
  }
  if (hero.highLow) {
    if (hero.longHighLow) {
      formatHighLow(text, sizeof(text), c.forecast.days[0]);
    } else {
      formatPair(text, sizeof(text), c.forecast.days[0]);
    }
    drawFitted(c, hero.font, textX, textY, text, hero.textWidth);
  }
}

// The icon and temperature in a row (or the icon above), with the condition
// and today's high/low under them as room allows.
struct Compact {
  int icon = 0;        // 0: the temperature alone
  bool above = false;  // the icon above the temperature
  bool condition = false;
  bool highLow = false;
  int room = 0;  // what the lines under the row are cut to
  int rowWidth = 0;
  int rowHeight = 0;
  int width = 0;
  int height = 0;  // 0 when nothing fits
};

struct CompactOption {
  int icon;
  bool above;
  bool condition;
};
// Best first. The big icon only comes with the condition: that says more.
constexpr CompactOption COMPACT_OPTIONS[] = {
    {ui::HERO_ICON, false, true},
    {ui::SMALL_ICON, false, true},
    {ui::SMALL_ICON, false, false},
    {ui::SMALL_ICON, true, true},
    {ui::SMALL_ICON, true, false},
    {0, false, true},
    {0, false, false},
};

Compact planCompact(const Ctx& c, const int maxWidth, const int maxHeight, const bool withHighLow) {
  char text[48];
  formatTemperature(text, sizeof(text), c.forecast.temperature);
  const int tempWidth = c.width(ui::BIG_FONT_ID, text, true);
  const int bigLine = c.line(ui::BIG_FONT_ID);
  const int line = c.line(COMPACT_FONT);
  const int conditionWidth = c.width(COMPACT_FONT, conditionOf(c), true);

  Compact compact;
  for (const auto& option : COMPACT_OPTIONS) {
    int rowWidth = tempWidth;
    int rowHeight = bigLine;
    if (option.icon > 0 && option.above) {
      rowWidth = std::max(option.icon, tempWidth);
      rowHeight = option.icon + bigLine;
    } else if (option.icon > 0) {
      rowWidth = option.icon + c.rowGap + tempWidth;
      rowHeight = std::max(option.icon, bigLine);
    }
    const int height = rowHeight + (option.condition ? line : 0);
    if (rowWidth > maxWidth || height > maxHeight) continue;

    compact.icon = option.icon;
    compact.above = option.above;
    compact.condition = option.condition;
    compact.room = maxWidth;
    compact.rowWidth = rowWidth;
    compact.rowHeight = rowHeight;
    compact.width = option.condition ? std::max(rowWidth, std::min(conditionWidth, maxWidth)) : rowWidth;
    compact.height = height;
    if (withHighLow && option.condition && c.forecast.dayCount > 0 && height + line <= maxHeight) {
      formatPair(text, sizeof(text), c.forecast.days[0]);
      compact.highLow = true;
      compact.width = std::max(compact.width, std::min(c.width(COMPACT_FONT, text), maxWidth));
      compact.height += line;
    }
    return compact;
  }
  return compact;
}

// x is the left edge, or the centre when centered.
void drawCompact(const Ctx& c, const Compact& compact, const int x, const int y, const bool centered) {
  char text[48];
  formatTemperature(text, sizeof(text), c.forecast.temperature);
  const int tempWidth = c.width(ui::BIG_FONT_ID, text, true);
  const int bigLine = c.line(ui::BIG_FONT_ID);
  const int left = centered ? x - compact.rowWidth / 2 : x;
  const uint8_t* icon = compact.icon > 0 ? iconBitmap(c.forecast.code, compact.icon) : nullptr;
  if (!icon) {
    c.renderer.drawText(ui::BIG_FONT_ID, left, y, text, true, EpdFontFamily::BOLD);
  } else if (compact.above) {
    c.renderer.drawIcon(icon, left + (compact.rowWidth - compact.icon) / 2, y, compact.icon);
    c.renderer.drawText(ui::BIG_FONT_ID, left + (compact.rowWidth - tempWidth) / 2, y + compact.icon, text, true,
                        EpdFontFamily::BOLD);
  } else {
    c.renderer.drawIcon(icon, left, y + (compact.rowHeight - compact.icon) / 2, compact.icon);
    c.renderer.drawText(ui::BIG_FONT_ID, left + compact.icon + c.rowGap, y + (compact.rowHeight - bigLine) / 2, text,
                        true, EpdFontFamily::BOLD);
  }

  const Align align = centered ? Align::Center : Align::Left;
  int lineY = y + compact.rowHeight;
  if (compact.condition) {
    drawFitted(c, COMPACT_FONT, x, lineY, conditionOf(c), compact.room, align, true);
    lineY += c.line(COMPACT_FONT);
  }
  if (compact.highLow) {
    formatPair(text, sizeof(text), c.forecast.days[0]);
    drawFitted(c, COMPACT_FONT, x, lineY, text, compact.room, align);
  }
}

// ---- Details ----

// Details row by row across up to maxColumns columns, as many rows as fit.
struct Grid {
  DetailSet set{nullptr, 0};
  int font = 0;
  int available = 0;  // details the forecast has
  int count = 0;      // details shown
  int columns = 0;
  int rows = 0;
  int widest = 0;
  int height = 0;
};

Grid planGrid(const Ctx& c, const DetailSet& set, const int font, const int width, const int maxHeight,
              const int maxColumns) {
  Grid grid;
  grid.set = set;
  grid.font = font;
  char text[48];
  for (int i = 0; i < set.count; i++) {
    if (!formatDetail(c, set.items[i], text, sizeof(text))) continue;
    grid.available++;
    grid.widest = std::max(grid.widest, c.width(font, text));
  }
  const int line = c.line(font);
  const int fitRows = (maxHeight + c.rowGap) / (line + c.rowGap);
  if (grid.available == 0 || fitRows <= 0 || width <= 0) return grid;
  const int fitColumns = std::clamp((width + c.gap) / (grid.widest + c.gap), 1, maxColumns);
  const int neededRows = (grid.available + fitColumns - 1) / fitColumns;
  grid.rows = std::min(fitRows, neededRows);
  // Evened out: four details read better as 2 x 2 than as 3 + 1.
  grid.columns = grid.rows == neededRows ? (grid.available + grid.rows - 1) / grid.rows : fitColumns;
  grid.count = std::min(grid.available, grid.rows * grid.columns);
  grid.height = grid.rows * line + (grid.rows - 1) * c.rowGap;
  return grid;
}

void drawGrid(const Ctx& c, const Grid& grid, const int x, const int y, const int width) {
  if (grid.count <= 0) return;
  char text[48];
  const int columnWidth = width / grid.columns;
  const int room = columnWidth - (grid.columns > 1 ? c.gap : 0);
  const int pitch = c.line(grid.font) + c.rowGap;
  int shown = 0;
  for (int i = 0; i < grid.set.count && shown < grid.count; i++) {
    if (!formatDetail(c, grid.set.items[i], text, sizeof(text))) continue;
    const int column = shown % grid.columns;
    const int row = shown / grid.columns;
    drawFitted(c, grid.font, x + column * columnWidth, y + row * pitch, text, room);
    shown++;
  }
}

// ---- The next days ----

// The next days' widest texts in each of DAY_FONTS.
struct DayMetrics {
  int line[DAY_FONT_COUNT] = {};
  int weekday[DAY_FONT_COUNT] = {};
  int pair[DAY_FONT_COUNT] = {};
  int rain = 0;  // widest rain line, in SMALL_FONT_ID; 0 when no day has one
  int rainLine = 0;
};

DayMetrics measureDays(const Ctx& c) {
  DayMetrics m;
  char text[32];
  for (int f = 0; f < DAY_FONT_COUNT; f++) {
    m.line[f] = c.line(DAY_FONTS[f]);
    for (int i = 1; i <= c.nextDays; i++) {
      const auto& day = c.forecast.days[i];
      m.weekday[f] = std::max(m.weekday[f], c.width(DAY_FONTS[f], weekdayOf(c, day), true));
      formatPair(text, sizeof(text), day);
      m.pair[f] = std::max(m.pair[f], c.width(DAY_FONTS[f], text));
    }
  }
  m.rainLine = c.line(SMALL_FONT_ID);
  for (int i = 1; i <= c.nextDays; i++) {
    if (formatRain(text, sizeof(text), c.forecast.days[i])) m.rain = std::max(m.rain, c.width(SMALL_FONT_ID, text));
  }
  return m;
}

// Columns side by side (weekday, icon, high/low, rain), or a list with a row
// per day (weekday, icon, high/low, rain on the right).
struct Days {
  bool list = false;
  bool stacked = false;  // list rows as the weekday over the high/low
  bool rain = false;
  int face = 0;   // index into DAY_FONTS
  int count = 0;  // 0 when nothing fits
  int icon = 0;
  int pitch = 0;      // columns: a column's width
  int rowHeight = 0;  // list: a row's height
  int height = 0;     // the block, list rows unspread
};

// Enough days first, then icons, more days, bigger icons, one-line rows, the
// rain, the bigger face.
bool better(const Days& a, const Days& b, const int enough) {
  const bool aEnough = a.count >= enough;
  const bool bEnough = b.count >= enough;
  if (aEnough != bEnough) return aEnough;
  if (!aEnough && a.count != b.count) return a.count > b.count;
  if ((a.icon > 0) != (b.icon > 0)) return a.icon > 0;
  if (a.count != b.count) return a.count > b.count;
  if (a.icon != b.icon) return a.icon > b.icon;
  if (a.stacked != b.stacked) return !a.stacked;
  if (a.rain != b.rain) return a.rain;
  return a.face < b.face;
}

Days planColumns(const Ctx& c, const DayMetrics& m, const int width, const int height, const int enough) {
  Days best;
  for (int f = 0; f < DAY_FONT_COUNT; f++) {
    const int base = m.line[f] * 2 + c.rowGap;  // weekday, high/low
    const int count = std::min(c.nextDays, (width + c.gap) / (std::max(m.weekday[f], m.pair[f]) + c.gap));
    if (base > height || count <= 0) continue;
    Days days;
    days.face = f;
    days.count = count;
    days.pitch = width / count;
    const int room = days.pitch - c.gap;
    const int rainHeight = m.rain > 0 && m.rain <= room ? c.rowGap + m.rainLine : 0;
    // The big icon only when the rain still fits under it.
    if (ui::HERO_ICON <= room && base + c.rowGap + ui::HERO_ICON + rainHeight <= height) {
      days.icon = ui::HERO_ICON;
    } else if (ui::SMALL_ICON <= room && base + c.rowGap + ui::SMALL_ICON <= height) {
      days.icon = ui::SMALL_ICON;
    }
    days.height = base + (days.icon > 0 ? c.rowGap + days.icon : 0);
    days.rain = rainHeight > 0 && days.height + rainHeight <= height;
    if (days.rain) days.height += rainHeight;
    if (best.count == 0 || better(days, best, enough)) best = days;
  }
  return best;
}

Days planList(const Ctx& c, const DayMetrics& m, const int width, const int height, const int enough) {
  static constexpr int ICONS[] = {ui::HERO_ICON, ui::SMALL_ICON, 0};
  Days best;
  const auto consider = [&](Days days) {
    days.list = true;
    days.count = std::min(c.nextDays, (height + c.rowGap) / (days.rowHeight + c.rowGap));
    if (days.count <= 0) return;
    days.height = days.count * (days.rowHeight + c.rowGap) - c.rowGap;
    if (best.count == 0 || better(days, best, enough)) best = days;
  };
  for (int f = 0; f < DAY_FONT_COUNT; f++) {
    const int text = m.weekday[f] + c.rowGap + m.pair[f];
    for (const int icon : ICONS) {
      const int rowWidth = text + (icon > 0 ? icon + c.rowGap : 0);
      if (rowWidth > width) continue;
      Days days;
      days.face = f;
      days.icon = icon;
      days.rain = m.rain > 0 && rowWidth + c.gap + m.rain <= width;
      days.rowHeight = std::max(m.line[f], icon);
      consider(days);
    }
    // Too narrow for one line: the weekday over the high/low.
    if (text > width && std::max(m.weekday[f], m.pair[f]) <= width) {
      Days days;
      days.face = f;
      days.stacked = true;
      days.rowHeight = m.line[f] * 2;
      consider(days);
    }
  }
  return best;
}

Days planDays(const Ctx& c, const DayMetrics& m, const int width, const int height, const DayLayout layout) {
  if (c.nextDays <= 0 || width <= 0 || height <= 0) return {};
  const int enough = std::min(c.nextDays, 3);
  const Days columns = layout != DayLayout::List ? planColumns(c, m, width, height, enough) : Days{};
  const Days list = layout != DayLayout::Columns ? planList(c, m, width, height, enough) : Days{};
  if (list.count == 0) return columns;
  if (columns.count == 0) return list;
  if (better(columns, list, enough)) return columns;
  if (better(list, columns, enough)) return list;
  return width >= height ? columns : list;
}

// List rows spread to fill room, up to half as far apart again.
int rowPitch(const Ctx& c, const Days& days, const int room) {
  const int natural = days.rowHeight + c.rowGap;
  if (days.count <= 1) return natural;
  return std::clamp((room - days.rowHeight) / (days.count - 1), natural, natural * 3 / 2);
}

int blockHeight(const Ctx& c, const Days& days, const int room) {
  return days.list ? days.rowHeight + (days.count - 1) * rowPitch(c, days, room) : days.height;
}

void drawDays(const Ctx& c, const DayMetrics& m, const Days& days, const int x, const int y, const int width,
              const int room) {
  const int font = DAY_FONTS[days.face];
  const int line = m.line[days.face];
  char pair[32];
  char rain[32];
  if (!days.list) {
    const int columnRoom = days.pitch - c.gap;
    for (int i = 0; i < days.count; i++) {
      const auto& day = c.forecast.days[1 + i];
      const int centerX = x + days.pitch * i + days.pitch / 2;
      int rowY = y;
      drawFitted(c, font, centerX, rowY, weekdayOf(c, day), columnRoom, Align::Center, true);
      rowY += line + c.rowGap;
      if (days.icon > 0) {
        c.renderer.drawIcon(iconBitmap(day.code, days.icon), centerX - days.icon / 2, rowY, days.icon);
        rowY += days.icon + c.rowGap;
      }
      formatPair(pair, sizeof(pair), day);
      drawFitted(c, font, centerX, rowY, pair, columnRoom, Align::Center);
      rowY += line + c.rowGap;
      if (days.rain && formatRain(rain, sizeof(rain), day)) {
        drawFitted(c, SMALL_FONT_ID, centerX, rowY, rain, columnRoom, Align::Center);
      }
    }
    return;
  }

  const int pitch = rowPitch(c, days, room);
  const int right = x + width;
  const int weekdayWidth = std::min(m.weekday[days.face], width);
  // The rain on the text's baseline, kept inside its row.
  const int rainShift = c.renderer.getFontAscenderSize(font) - c.renderer.getFontAscenderSize(SMALL_FONT_ID);
  const int rainLowest = days.rowHeight - m.rainLine;
  for (int i = 0; i < days.count; i++) {
    const auto& day = c.forecast.days[1 + i];
    const int rowY = y + i * pitch;
    formatPair(pair, sizeof(pair), day);
    if (days.stacked) {
      drawFitted(c, font, x, rowY, weekdayOf(c, day), width, Align::Left, true);
      drawFitted(c, font, x, rowY + line, pair, width);
      continue;
    }
    const int textY = rowY + (days.rowHeight - line) / 2;
    drawFitted(c, font, x, textY, weekdayOf(c, day), weekdayWidth, Align::Left, true);
    int pairX = x + weekdayWidth + c.rowGap;
    if (days.icon > 0) {
      c.renderer.drawIcon(iconBitmap(day.code, days.icon), pairX, rowY + (days.rowHeight - days.icon) / 2, days.icon);
      pairX += days.icon + c.rowGap;
    }
    int pairRoom = right - pairX;
    if (days.rain) {
      pairRoom -= c.gap + m.rain;
      if (formatRain(rain, sizeof(rain), day)) {
        const int rainY = std::min(textY + rainShift, rowY + rainLowest);
        drawFitted(c, SMALL_FONT_ID, right, rainY, rain, m.rain, Align::Right);
      }
    }
    drawFitted(c, font, pairX, textY, pair, pairRoom);
  }
}

// The next days under a rule, planned for the room between top and bottom:
// right under top, or against bottom when anchorBottom.
void drawDaysSection(const Ctx& c, const DayMetrics& m, const int x, const int top, const int width,
                     const int bottom, const DayLayout layout, const bool anchorBottom) {
  const int room = bottom - top - c.sectionGap * 2;
  const Days days = planDays(c, m, width, room, layout);
  if (days.count == 0) return;
  const int blockTop = anchorBottom ? bottom - blockHeight(c, days, room) : top + c.sectionGap * 2;
  const int ruleY = blockTop - c.sectionGap;
  c.renderer.drawLine(x, ruleY, x + width - 1, ruleY, true);
  drawDays(c, m, days, x, blockTop, width, room);
}

// ---- Layouts ----

// Small square: the icon and temperature, centred, with the condition when it
// fits. The title goes only when an icon would not fit under it.
void drawSmall(const Ctx& c, const Rect& area) {
  Compact compact = planCompact(c, area.width, area.height - titleHeight(c), false);
  const Compact bare = planCompact(c, area.width, area.height, false);
  int top = area.y;
  if (compact.height > 0 && (compact.icon > 0 || bare.icon == 0)) {
    top = drawHeader(c, area);
  } else {
    compact = bare;
  }
  if (compact.height == 0) return;
  const int bottom = area.y + area.height;
  drawCompact(c, compact, area.x + area.width / 2, top + std::max(0, (bottom - top - compact.height) / 2), true);
}

// Square Medium: now, then feels like, rain and wind, then the next days if a
// row of them still fits.
void drawMedium(const Ctx& c, const Rect& area) {
  const int top = drawHeader(c, area);
  const int bottom = area.y + area.height;
  const Hero hero = planHero(c, area.width, bottom - top, UI_10_FONT_ID);
  drawHero(c, hero, area.x, top);
  int y = top + hero.height;
  const Grid grid = planGrid(c, MEDIUM_DETAILS, UI_10_FONT_ID, area.width, bottom - y - c.sectionGap, 1);
  if (grid.count > 0) {
    drawGrid(c, grid, area.x, y + c.sectionGap, area.width);
    y += c.sectionGap + grid.height;
  }
  drawDaysSection(c, measureDays(c), area.x, y, area.width, bottom, DayLayout::Best, true);
}

// Wide: now on the left, with details under it when there is room; the next
// days as columns across the rest. A short strip gets text-only columns.
void drawWide(const Ctx& c, const Rect& area) {
  const int top = drawHeader(c, area);
  const int bottom = area.y + area.height;
  const int half = area.width / 2;
  const Compact compact = planCompact(c, half, bottom - top, true);
  if (compact.height == 0) return;
  drawCompact(c, compact, area.x, top, false);

  int leftWidth = compact.width;
  if (c.nextDays > 0) {
    const int detailsTop = top + compact.height + c.sectionGap;
    const Grid grid = planGrid(c, MEDIUM_DETAILS, UI_10_FONT_ID, half, bottom - detailsTop, 1);
    if (grid.count > 0) {
      drawGrid(c, grid, area.x, detailsTop, half);
      leftWidth = std::max(leftWidth, std::min(grid.widest, half));
    }
  }

  const int ruleX = area.x + leftWidth + c.gap;
  const int regionX = ruleX + c.gap;
  const int regionWidth = area.x + area.width - regionX;
  if (c.nextDays > 0) {
    const DayMetrics m = measureDays(c);
    const Days days = planDays(c, m, regionWidth, bottom - top, DayLayout::Columns);
    if (days.count == 0) return;
    c.renderer.drawLine(ruleX, top, ruleX, bottom - 1, true);
    drawDays(c, m, days, regionX, top, regionWidth, bottom - top);
    return;
  }
  // No next days: the details take their place.
  const Grid grid = planGrid(c, LARGE_DETAILS, UI_10_FONT_ID, regionWidth, bottom - top, 3);
  if (grid.count == 0) return;
  c.renderer.drawLine(ruleX, top, ruleX, bottom - 1, true);
  drawGrid(c, grid, regionX, top, regionWidth);
}

// Tall: now on top, the next days as a list filling the rest. Medium also
// fits feels like, rain and wind between them when every day has its row.
void drawTall(const Ctx& c, const Rect& area, const bool compactHero) {
  const int top = drawHeader(c, area);
  const int bottom = area.y + area.height;
  int y = top;
  if (compactHero) {
    const Compact compact = planCompact(c, area.width, bottom - top, false);
    if (compact.height == 0) return;
    drawCompact(c, compact, area.x + area.width / 2, top, true);
    y += compact.height;
  } else {
    const Hero hero = planHero(c, area.width, bottom - top, UI_10_FONT_ID);
    drawHero(c, hero, area.x, top);
    y += hero.height;
  }

  const DayMetrics m = measureDays(c);
  if (!compactHero) {
    const int room = bottom - y - c.sectionGap * 2;
    const Days days = planDays(c, m, area.width, room, DayLayout::List);
    if (days.count == c.nextDays) {
      const int spare = room - days.height - c.sectionGap;
      const Grid grid = planGrid(c, MEDIUM_DETAILS, UI_10_FONT_ID, area.width, spare, 1);
      if (grid.count > 0) {
        drawGrid(c, grid, area.x, y + c.sectionGap, area.width);
        y += c.sectionGap + grid.height;
      }
    }
  }
  drawDaysSection(c, m, area.x, y, area.width, bottom, DayLayout::List, false);
}

// Large: now, every detail (beside it when two columns fit there, else under
// it), and the next days against the bottom as a list or columns.
void drawLarge(const Ctx& c, const Rect& area) {
  const int top = drawHeader(c, area);
  const int bottom = area.y + area.height;
  const Hero hero = planHero(c, area.width, bottom - top, UI_12_FONT_ID);
  drawHero(c, hero, area.x, top);
  const DayMetrics m = measureDays(c);
  int y = top + hero.height;

  const int besideX = area.x + hero.width + c.gap * 2;
  const int besideWidth = area.x + area.width - besideX;
  for (const int font : DETAIL_FONTS) {
    const Grid grid = planGrid(c, LARGE_DETAILS, font, besideWidth, hero.height, 3);
    if (grid.columns >= 2 && grid.count == grid.available) {
      drawGrid(c, grid, besideX, top + (hero.height - grid.height) / 2, besideWidth);
      drawDaysSection(c, m, area.x, y, area.width, bottom, DayLayout::Best, true);
      return;
    }
  }

  // Under it: the larger face unless it costs the next days a row or their icons.
  y += c.sectionGap;
  const Grid largeGrid = planGrid(c, LARGE_DETAILS, DETAIL_FONTS[0], area.width, bottom - y, 3);
  const Grid smallGrid = planGrid(c, LARGE_DETAILS, DETAIL_FONTS[1], area.width, bottom - y, 3);
  const int daysRoom = bottom - y - c.sectionGap * 2;
  const Days largeDays = planDays(c, m, area.width, daysRoom - largeGrid.height, DayLayout::Best);
  const Days smallDays = planDays(c, m, area.width, daysRoom - smallGrid.height, DayLayout::Best);
  const bool smaller = smallGrid.count > largeGrid.count || better(smallDays, largeDays, std::min(c.nextDays, 3));
  const Grid& grid = smaller ? smallGrid : largeGrid;
  if (grid.count > 0) {
    drawGrid(c, grid, area.x, y, area.width);
    y += grid.height;
  }
  drawDaysSection(c, m, area.x, y, area.width, bottom, DayLayout::Best, true);
}

}  // namespace

void drawWeather(GfxRenderer& renderer, const Rect& area, const WeatherService& weather) {
  const auto& forecast = weather.forecast();
  const int nextDays = std::clamp(static_cast<int>(forecast.dayCount) - 1, 0, WeatherForecast::MAX_DAYS - 1);
  const int gap = UITheme::scaledPx(16);
  const Ctx c{renderer, weather, forecast, nextDays, gap, UITheme::scaledPx(8), gap};
  if (!weather.hasLocation() || !forecast.valid) {
    drawState(c, area);
    return;
  }

  const TileSize size = sizeOf(area);
  if (size.scale == TileSize::Scale::Large) {
    drawLarge(c, area);
  } else if (size.shape == TileSize::Shape::Wide) {
    drawWide(c, area);
  } else if (size.shape == TileSize::Shape::Tall) {
    drawTall(c, area, size.scale == TileSize::Scale::Small);
  } else if (size.scale == TileSize::Scale::Small) {
    drawSmall(c, area);
  } else {
    drawMedium(c, area);
  }
}

}  // namespace dashboard_tiles
