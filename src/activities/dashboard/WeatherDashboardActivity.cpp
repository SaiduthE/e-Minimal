#include "WeatherDashboardActivity.h"

#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <I18n.h>

#include <cmath>
#include <cstdio>

#include "DateNames.h"
#include "MappedInputManager.h"
#include "WeatherIcons.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr int HERO_ICON = 160;
constexpr int DAY_ICON = 96;
constexpr int BIG_FONT_ID = NOTOSERIF_24_FONT_ID;
// Feels like, humidity, wind and the rest: a step under the condition.
constexpr int DETAIL_FONT_ID = UI_10_FONT_ID;

int rounded(const float value) { return static_cast<int>(std::lround(value)); }
}  // namespace

void WeatherDashboardActivity::onEnter() {
  TimedDashboardActivity::onEnter();
  weather.load();
  requestUpdate();
}

void WeatherDashboardActivity::fetch(const bool announce) {
  if (announce || !weather.forecast().valid) {
    weather.markUpdating();
    // Immediate: the loop is about to block, so a deferred request would wait for it.
    requestUpdate(true);
  }
  weather.fetch();
  requestUpdate();
}

void WeatherDashboardActivity::update() { fetch(false); }

bool WeatherDashboardActivity::handleKeys() {
  if (!mappedInput.wasReleased(MappedInputManager::Button::Confirm)) return false;
  if (weather.hasLocation()) fetch(true);
  return true;
}

void WeatherDashboardActivity::drawCentered(const int fontId, const int centerX, const int y, const char* text,
                                            const bool bold) const {
  const auto style = bold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
  renderer.drawText(fontId, centerX - renderer.getTextWidth(fontId, text, style) / 2, y, text, true, style);
}

void WeatherDashboardActivity::drawMessage(const char* message, const int top, const int bottom) const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int width = renderer.getScreenWidth() - metrics.contentSidePadding * 2;
  const auto lines = renderer.wrappedText(UI_12_FONT_ID, message, width, 4);
  const int lineHeight = renderer.getLineHeight(UI_12_FONT_ID);
  int y = top + (bottom - top - static_cast<int>(lines.size()) * lineHeight) / 2;
  for (const auto& line : lines) {
    drawCentered(UI_12_FONT_ID, renderer.getScreenWidth() / 2, y, line.c_str(), false);
    y += lineHeight;
  }
}

void WeatherDashboardActivity::drawForecast(const int x, const int top, const int width, const int bottom) const {
  const auto& forecast = weather.forecast();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int gap = UITheme::scaledPx(24);
  const int uiLine = renderer.getLineHeight(UI_12_FONT_ID);
  const int bigLine = renderer.getLineHeight(BIG_FONT_ID);
  char text[64];

  // Now: icon, temperature, condition, today's high and low.
  renderer.drawIcon(weather_icons::hero(forecast.code), x, top, HERO_ICON);
  const int textX = x + HERO_ICON + gap;
  int y = top + (HERO_ICON - bigLine - uiLine * 2) / 2;
  snprintf(text, sizeof(text), "%d\xC2\xB0", rounded(forecast.temperature));
  renderer.drawText(BIG_FONT_ID, textX, y, text, true, EpdFontFamily::BOLD);
  y += bigLine;
  renderer.drawText(UI_12_FONT_ID, textX, y, I18N.get(weather::conditionFor(forecast.code)), true,
                    EpdFontFamily::BOLD);
  y += uiLine;
  if (forecast.dayCount > 0) {
    snprintf(text, sizeof(text), tr(STR_DASH_HIGH_LOW_FORMAT), rounded(forecast.days[0].high),
             rounded(forecast.days[0].low));
    renderer.drawText(UI_12_FONT_ID, textX, y, text);
  }

  // Details in two columns under it, as far as they fit above the days.
  const int daysHeight = uiLine * 3 + DAY_ICON + renderer.getLineHeight(SMALL_FONT_ID) + metrics.verticalSpacing * 4;
  const int daysTop = bottom - daysHeight;
  const int detailLine = renderer.getLineHeight(DETAIL_FONT_ID);
  const int rowHeight = detailLine + metrics.verticalSpacing;
  y = top + HERO_ICON + metrics.verticalSpacing * 3;
  const int columnX[2] = {x, x + width / 2};
  char details[6][48];
  int count = 0;
  snprintf(details[count++], sizeof(details[0]), tr(STR_DASH_FEELS_FORMAT), rounded(forecast.feelsLike));
  if (forecast.humidity >= 0) {
    snprintf(details[count++], sizeof(details[0]), tr(STR_DASH_HUMIDITY_FORMAT), static_cast<int>(forecast.humidity));
  }
  snprintf(details[count++], sizeof(details[0]), tr(STR_DASH_WIND_FORMAT), rounded(forecast.windSpeed),
           weather.fahrenheit() ? tr(STR_UNIT_MPH) : tr(STR_UNIT_KMH));
  if (forecast.dayCount > 0) {
    const auto& today = forecast.days[0];
    if (today.precipitationChance >= 0) {
      snprintf(details[count++], sizeof(details[0]), tr(STR_DASH_RAIN_FORMAT),
               static_cast<int>(today.precipitationChance));
    }
    if (today.sunrise != 0 && today.sunset != 0) {
      const tm rise = weather::localAt(today.sunrise, forecast.utcOffset);
      const tm set = weather::localAt(today.sunset, forecast.utcOffset);
      snprintf(details[count++], sizeof(details[0]), tr(STR_DASH_SUNRISE_FORMAT), rise.tm_hour, rise.tm_min);
      snprintf(details[count++], sizeof(details[0]), tr(STR_DASH_SUNSET_FORMAT), set.tm_hour, set.tm_min);
    }
  }
  for (int i = 0; i < count && y + detailLine <= daysTop; i += 2) {
    renderer.drawText(DETAIL_FONT_ID, columnX[0], y, details[i]);
    if (i + 1 < count) renderer.drawText(DETAIL_FONT_ID, columnX[1], y, details[i + 1]);
    y += rowHeight;
  }

  drawDays(x, daysTop, width, bottom);
}

void WeatherDashboardActivity::drawDays(const int x, const int top, const int width, const int bottom) const {
  // The days after today, one column each.
  const auto& forecast = weather.forecast();
  const int first = 1;
  const int columns = forecast.dayCount - first;
  if (columns <= 0) return;
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int uiLine = renderer.getLineHeight(UI_12_FONT_ID);
  const int columnWidth = width / columns;
  char text[48];

  int y = top;
  renderer.drawText(UI_12_FONT_ID, x, y, tr(STR_DASH_NEXT_DAYS), true, EpdFontFamily::BOLD);
  y += uiLine;
  renderer.drawLine(x, y, x + width, y, 2, true);
  y += metrics.verticalSpacing * 2;

  for (int i = 0; i < columns; i++) {
    const auto& day = forecast.days[first + i];
    const int centerX = x + columnWidth * i + columnWidth / 2;
    int rowY = y;
    const tm date = weather::localAt(day.date, forecast.utcOffset);
    drawCentered(UI_12_FONT_ID, centerX, rowY, date_names::weekdayShort(date.tm_wday), true);
    rowY += uiLine + metrics.verticalSpacing;
    renderer.drawIcon(weather_icons::small(day.code), centerX - DAY_ICON / 2, rowY, DAY_ICON);
    rowY += DAY_ICON + metrics.verticalSpacing;
    snprintf(text, sizeof(text), tr(STR_DASH_TEMP_PAIR_FORMAT), rounded(day.high), rounded(day.low));
    drawCentered(UI_12_FONT_ID, centerX, rowY, text, false);
    rowY += uiLine;
    if (day.precipitationChance >= 0 && rowY + renderer.getLineHeight(SMALL_FONT_ID) <= bottom) {
      snprintf(text, sizeof(text), tr(STR_DASH_RAIN_FORMAT), static_cast<int>(day.precipitationChance));
      drawCentered(SMALL_FONT_ID, centerX, rowY, text, false);
    }
  }
}

void WeatherDashboardActivity::draw(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto& forecast = weather.forecast();
  const auto status = weather.status();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                 weather.place().empty() ? tr(STR_DASH_WEATHER) : weather.place().c_str());

  char dateLine[64] = "";
  char statusLine[48] = "";
  if (forecast.valid) {
    const tm observed = weather::localAt(forecast.observedAt, forecast.utcOffset);
    snprintf(dateLine, sizeof(dateLine), tr(STR_DASH_DATE_FORMAT), date_names::weekday(observed.tm_wday),
             observed.tm_mday, date_names::month(observed.tm_mon));
    snprintf(statusLine, sizeof(statusLine), tr(STR_DASH_UPDATED_FORMAT), observed.tm_hour, observed.tm_min);
  }
  switch (status) {
    case WeatherService::Status::Updating:
      snprintf(statusLine, sizeof(statusLine), "%s", tr(STR_DASH_UPDATING));
      break;
    case WeatherService::Status::Failed:
      snprintf(statusLine, sizeof(statusLine), "%s", tr(STR_DASH_UPDATE_FAILED));
      break;
    case WeatherService::Status::NoWifi:
      snprintf(statusLine, sizeof(statusLine), "%s", tr(STR_DASH_NO_WIFI_SHORT));
      break;
    default:
      break;
  }
  GUI.drawSubHeader(renderer, Rect{0, metrics.topPadding + metrics.headerHeight, pageWidth, metrics.tabBarHeight},
                    dateLine, statusLine);

  // Landscape: no key hints (they would be drawn sideways along the key edge),
  // so the content runs to the bottom margin.
  const int top = metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight + metrics.verticalSpacing * 3;
  const int bottom = pageHeight - metrics.verticalSpacing * 3;
  const int x = metrics.contentSidePadding;
  const int width = pageWidth - metrics.contentSidePadding * 2;

  if (status == WeatherService::Status::NoLocation) {
    drawMessage(tr(STR_DASH_WEATHER_NO_PLACE_LONG), top, bottom);
  } else if (!forecast.valid) {
    drawMessage(status == WeatherService::Status::NoWifi   ? tr(STR_DASH_WEATHER_NO_WIFI)
                : status == WeatherService::Status::Failed ? tr(STR_DASH_WEATHER_FAILED)
                                                           : tr(STR_DASH_UPDATING),
                top, bottom);
  } else {
    drawForecast(x, top, width, bottom);
  }

  // A clean waveform for the forecast; the transient "Updating" can be quick.
  renderer.displayBuffer(status == WeatherService::Status::Updating ? HalDisplay::FAST_REFRESH
                                                                    : HalDisplay::HALF_REFRESH);
}
