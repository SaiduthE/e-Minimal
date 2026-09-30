#pragma once

#include <cstdint>

#include "WeatherForecast.h"
#include "components/icons/weatherIcons.h"

// The condition icon for a WMO weather code, at the two sizes the dashboard
// draws (drawIcon() layout).
namespace weather_icons {

inline const uint8_t* hero(const int16_t code) {
  static constexpr const uint8_t* ICONS[] = {
      WeatherSunIcon160,          WeatherCloudSunIcon160,  WeatherCloudIcon160,     WeatherCloudFogIcon160,
      WeatherCloudDrizzleIcon160, WeatherCloudRainIcon160, WeatherCloudSnowIcon160, WeatherCloudLightningIcon160};
  return ICONS[static_cast<int>(weather::iconFor(code))];
}

inline const uint8_t* small(const int16_t code) {
  static constexpr const uint8_t* ICONS[] = {
      WeatherSunIcon96,          WeatherCloudSunIcon96,  WeatherCloudIcon96,     WeatherCloudFogIcon96,
      WeatherCloudDrizzleIcon96, WeatherCloudRainIcon96, WeatherCloudSnowIcon96, WeatherCloudLightningIcon96};
  return ICONS[static_cast<int>(weather::iconFor(code))];
}

}  // namespace weather_icons
