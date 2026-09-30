#include "WeatherForecast.h"

#include <Logging.h>

#include <cmath>
#include <cstdio>

namespace {
constexpr float LOCATION_EPSILON = 0.0005f;

template <typename T>
T valueAt(JsonArrayConst array, const size_t index, const T fallback) {
  JsonVariantConst value = array[index];
  return value.is<T>() ? value.as<T>() : fallback;
}
}  // namespace

bool WeatherForecast::matches(const float lat, const float lon, const bool wantFahrenheit) const {
  return valid && fahrenheit == wantFahrenheit && std::fabs(latitude - lat) < LOCATION_EPSILON &&
         std::fabs(longitude - lon) < LOCATION_EPSILON;
}

void WeatherForecast::toJson(JsonDocument& doc) const {
  doc["lat"] = latitude;
  doc["lon"] = longitude;
  doc["f"] = fahrenheit;
  doc["observedAt"] = observedAt;
  doc["utcOffset"] = utcOffset;
  doc["temp"] = temperature;
  doc["feels"] = feelsLike;
  doc["wind"] = windSpeed;
  doc["code"] = code;
  doc["humidity"] = humidity;
  JsonArray list = doc["days"].to<JsonArray>();
  for (uint8_t i = 0; i < dayCount; i++) {
    JsonObject day = list.add<JsonObject>();
    day["date"] = days[i].date;
    day["code"] = days[i].code;
    day["high"] = days[i].high;
    day["low"] = days[i].low;
    day["rain"] = days[i].precipitationChance;
    day["sunrise"] = days[i].sunrise;
    day["sunset"] = days[i].sunset;
  }
}

bool WeatherForecast::fromJson(JsonVariantConst doc) {
  *this = WeatherForecast{};
  if (!doc["observedAt"].is<int64_t>()) return false;
  latitude = doc["lat"] | 0.0f;
  longitude = doc["lon"] | 0.0f;
  fahrenheit = doc["f"] | false;
  observedAt = doc["observedAt"].as<int64_t>();
  utcOffset = doc["utcOffset"] | 0;
  temperature = doc["temp"] | 0.0f;
  feelsLike = doc["feels"] | 0.0f;
  windSpeed = doc["wind"] | 0.0f;
  code = doc["code"] | static_cast<int16_t>(-1);
  humidity = doc["humidity"] | static_cast<int16_t>(-1);
  for (JsonObjectConst day : doc["days"].as<JsonArrayConst>()) {
    if (dayCount >= MAX_DAYS) break;
    Day& d = days[dayCount++];
    d.date = day["date"] | static_cast<int64_t>(0);
    d.code = day["code"] | static_cast<int16_t>(-1);
    d.high = day["high"] | 0.0f;
    d.low = day["low"] | 0.0f;
    d.precipitationChance = day["rain"] | static_cast<int16_t>(-1);
    d.sunrise = day["sunrise"] | static_cast<int64_t>(0);
    d.sunset = day["sunset"] | static_cast<int64_t>(0);
  }
  valid = true;
  return true;
}

namespace weather {

bool clockValid() {
  // 2024-01-01: anything earlier means the system clock was never set.
  constexpr time_t VALID_AFTER = 1704067200;
  return time(nullptr) > VALID_AFTER;
}

tm localAt(const int64_t unixSeconds, const int32_t utcOffset) {
  const time_t local = static_cast<time_t>(unixSeconds + utcOffset);
  tm out{};
  gmtime_r(&local, &out);
  return out;
}

std::string forecastUrl(const float latitude, const float longitude, const bool fahrenheit) {
  char url[512];
  snprintf(url, sizeof(url),
           "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
           "&current=temperature_2m,apparent_temperature,relative_humidity_2m,weather_code,wind_speed_10m"
           "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max,sunrise,sunset"
           "&timezone=auto&timeformat=unixtime&forecast_days=%u%s",
           latitude, longitude, static_cast<unsigned>(WeatherForecast::MAX_DAYS),
           fahrenheit ? "&temperature_unit=fahrenheit&wind_speed_unit=mph" : "");
  return url;
}

bool parseResponse(const std::string& body, const float latitude, const float longitude, const bool fahrenheit,
                   WeatherForecast& out) {
  out = WeatherForecast{};
  JsonDocument doc;
  const DeserializationError error = deserializeJson(doc, body);
  if (error) {
    LOG_ERR("WX", "Forecast JSON: %s", error.c_str());
    return false;
  }
  JsonVariantConst current = doc["current"];
  if (!current["time"].is<int64_t>() || !current["temperature_2m"].is<float>()) {
    LOG_ERR("WX", "Forecast has no current reading");
    return false;
  }

  out.latitude = latitude;
  out.longitude = longitude;
  out.fahrenheit = fahrenheit;
  out.observedAt = current["time"].as<int64_t>();
  out.utcOffset = doc["utc_offset_seconds"] | 0;
  out.temperature = current["temperature_2m"].as<float>();
  out.feelsLike = current["apparent_temperature"] | out.temperature;
  out.windSpeed = current["wind_speed_10m"] | 0.0f;
  out.code = current["weather_code"] | static_cast<int16_t>(-1);
  out.humidity = current["relative_humidity_2m"] | static_cast<int16_t>(-1);

  JsonVariantConst daily = doc["daily"];
  JsonArrayConst dates = daily["time"].as<JsonArrayConst>();
  const size_t count = dates.size() < WeatherForecast::MAX_DAYS ? dates.size() : WeatherForecast::MAX_DAYS;
  for (size_t i = 0; i < count; i++) {
    WeatherForecast::Day& day = out.days[out.dayCount++];
    day.date = valueAt<int64_t>(dates, i, 0);
    day.code = valueAt<int16_t>(daily["weather_code"].as<JsonArrayConst>(), i, -1);
    day.high = valueAt<float>(daily["temperature_2m_max"].as<JsonArrayConst>(), i, 0.0f);
    day.low = valueAt<float>(daily["temperature_2m_min"].as<JsonArrayConst>(), i, 0.0f);
    day.precipitationChance = valueAt<int16_t>(daily["precipitation_probability_max"].as<JsonArrayConst>(), i, -1);
    day.sunrise = valueAt<int64_t>(daily["sunrise"].as<JsonArrayConst>(), i, 0);
    day.sunset = valueAt<int64_t>(daily["sunset"].as<JsonArrayConst>(), i, 0);
  }
  out.valid = true;
  return true;
}

// WMO weather interpretation codes, as Open-Meteo documents them.
Icon iconFor(const int16_t code) {
  switch (code) {
    case 0:
      return Icon::Sun;
    case 1:
    case 2:
      return Icon::CloudSun;
    case 45:
    case 48:
      return Icon::Fog;
    case 51:
    case 53:
    case 55:
    case 56:
    case 57:
      return Icon::Drizzle;
    case 61:
    case 63:
    case 65:
    case 66:
    case 67:
    case 80:
    case 81:
    case 82:
      return Icon::Rain;
    case 71:
    case 73:
    case 75:
    case 77:
    case 85:
    case 86:
      return Icon::Snow;
    case 95:
    case 96:
    case 99:
      return Icon::Thunder;
    default:
      return Icon::Cloud;
  }
}

StrId conditionFor(const int16_t code) {
  switch (code) {
    case 0:
      return StrId::STR_WX_CLEAR;
    case 1:
      return StrId::STR_WX_MAINLY_CLEAR;
    case 2:
      return StrId::STR_WX_PARTLY_CLOUDY;
    case 3:
      return StrId::STR_WX_OVERCAST;
    case 45:
    case 48:
      return StrId::STR_WX_FOG;
    case 51:
    case 53:
    case 55:
      return StrId::STR_WX_DRIZZLE;
    case 56:
    case 57:
      return StrId::STR_WX_FREEZING_DRIZZLE;
    case 61:
    case 63:
    case 65:
      return StrId::STR_WX_RAIN;
    case 66:
    case 67:
      return StrId::STR_WX_FREEZING_RAIN;
    case 71:
    case 73:
    case 75:
    case 77:
      return StrId::STR_WX_SNOW;
    case 80:
    case 81:
    case 82:
      return StrId::STR_WX_RAIN_SHOWERS;
    case 85:
    case 86:
      return StrId::STR_WX_SNOW_SHOWERS;
    case 95:
    case 96:
    case 99:
      return StrId::STR_WX_THUNDERSTORM;
    default:
      return StrId::STR_WX_UNKNOWN;
  }
}

}  // namespace weather
