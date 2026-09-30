#pragma once

#include <ArduinoJson.h>
#include <I18n.h>

#include <cstddef>
#include <cstdint>
#include <ctime>
#include <string>

// One Open-Meteo forecast (/v1/forecast with timeformat=unixtime and
// timezone=auto), reduced to what the Weather dashboard draws. Times are unix
// seconds; add utcOffset for the location's wall clock.
struct WeatherForecast {
  static constexpr uint8_t MAX_DAYS = 5;

  struct Day {
    int64_t date = 0;  // local midnight
    int16_t code = -1;  // WMO weather code, -1 when missing
    float high = 0.0f;
    float low = 0.0f;
    int16_t precipitationChance = -1;  // percent, -1 when missing
    int64_t sunrise = 0;
    int64_t sunset = 0;
  };

  bool valid = false;
  // The location and units asked for, so a cache for another place is ignored.
  float latitude = 0.0f;
  float longitude = 0.0f;
  bool fahrenheit = false;
  // current.time: the start of the 15-minute slot the reading belongs to.
  int64_t observedAt = 0;
  int32_t utcOffset = 0;
  float temperature = 0.0f;
  float feelsLike = 0.0f;
  float windSpeed = 0.0f;
  int16_t code = -1;
  int16_t humidity = -1;
  Day days[MAX_DAYS];
  uint8_t dayCount = 0;

  bool matches(float lat, float lon, bool wantFahrenheit) const;
  // The on-card cache (/.crosspoint/weather.json), written after each fetch
  // so a reboot shows the last forecast at once.
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);
};

namespace weather {

constexpr const char* CACHE_PATH = "/.crosspoint/weather.json";

// False until something set the system clock (a forecast fetch does).
bool clockValid();
// The wall clock at utcOffset for a unix time.
tm localAt(int64_t unixSeconds, int32_t utcOffset);

std::string forecastUrl(float latitude, float longitude, bool fahrenheit);
// Parses an Open-Meteo response body into out (location and units are the
// caller's). False, leaving out invalid, on anything unusable.
bool parseResponse(const std::string& body, float latitude, float longitude, bool fahrenheit, WeatherForecast& out);

enum class Icon : uint8_t { Sun, CloudSun, Cloud, Fog, Drizzle, Rain, Snow, Thunder };
Icon iconFor(int16_t code);
StrId conditionFor(int16_t code);

}  // namespace weather
