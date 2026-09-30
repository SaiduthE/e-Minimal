#pragma once

#include <string>

#include "WeatherForecast.h"

// The weather widget's data: the place from DashboardConfigStore, the forecast
// cached on the card, and the fetch over a saved WiFi network: when the cache
// is stale, then once a day (early morning, local time, when the clock is
// known; else every 24 h), retrying hourly a few times after a failure. The
// dashboard's tile and the full forecast each own one; both read the same
// cache, so a fetch by one shows in the other on its next load().
class WeatherService {
 public:
  enum class Status : uint8_t { Ready, NoLocation, NoWifi, Updating, Failed };

  void load();
  // millis() of the next fetch; 0 when none is scheduled (no place).
  unsigned long nextFetchMs() const { return nextFetch; }
  // Shows "Updating" until fetch() ends. The caller repaints around both.
  void markUpdating() { setStatus(Status::Updating); }
  // Blocking: connect, fetch, parse, cache, reschedule. True when anything the
  // widget shows changed (the forecast or the status line).
  bool fetch();

  // For drawing, under the render lock (the loop task writes under it).
  const WeatherForecast& forecast() const { return current; }
  Status status() const { return state; }
  bool hasLocation() const { return located; }
  const std::string& place() const { return placeName; }
  bool fahrenheit() const { return useFahrenheit; }

 private:
  static constexpr int FETCH_HOUR = 5;  // local
  static constexpr int64_t STALE_AFTER_S = 20 * 60 * 60;
  static constexpr unsigned long DAY_MS = 24UL * 60 * 60 * 1000;
  static constexpr unsigned long RETRY_MS = 60UL * 60 * 1000;
  static constexpr uint8_t MAX_RETRIES = 3;

  void setStatus(Status next);
  void scheduleDaily();

  WeatherForecast current;
  Status state = Status::Ready;
  unsigned long nextFetch = 0;
  uint8_t failures = 0;

  std::string placeName;
  float latitude = 0.0f;
  float longitude = 0.0f;
  bool useFahrenheit = false;
  bool located = false;
};
