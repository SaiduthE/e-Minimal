#include "WeatherService.h"

#include <Arduino.h>
#include <HalStorage.h>
#include <Logging.h>
#include <PersistableStore.h>
#include <sys/time.h>

#include <cmath>

#include "DashboardConfigStore.h"
#include "activities/RenderLock.h"
#include "network/HttpDownloader.h"
#include "network/SavedWifi.h"

void WeatherService::load() {
  DASHBOARD_CONFIG.loadFromFile();
  const auto& config = DASHBOARD_CONFIG;
  WeatherForecast cached;
  if (config.weatherHasLocation && Storage.exists(weather::CACHE_PATH)) {
    JsonDocument doc;
    if (!PersistableStoreBase::readDocFromFile(weather::CACHE_PATH, doc) ||
        !cached.fromJson(doc.as<JsonVariantConst>()) ||
        !cached.matches(config.weatherLatitude, config.weatherLongitude, config.weatherFahrenheit)) {
      cached = WeatherForecast{};
    }
  }

  {
    RenderLock lock;
    located = config.weatherHasLocation;
    placeName = config.weatherPlace;
    latitude = config.weatherLatitude;
    longitude = config.weatherLongitude;
    useFahrenheit = config.weatherFahrenheit;
    current = cached;
    state = located ? Status::Ready : Status::NoLocation;
  }
  failures = 0;

  if (!located) {
    nextFetch = 0;
  } else if (current.valid && weather::clockValid() && time(nullptr) - current.observedAt < STALE_AFTER_S) {
    scheduleDaily();
  } else {
    // Stale, for another place, or the clock is unknown: fetch on the next pass.
    nextFetch = millis();
  }
}

void WeatherService::setStatus(const Status next) {
  RenderLock lock;
  state = next;
}

bool WeatherService::fetch() {
  if (!located) return false;
  if (!saved_wifi::hasSavedNetworks()) {
    LOG_DBG("WX", "No saved WiFi network");
    setStatus(Status::NoWifi);
    // Checked again once a day, in case a network was saved meanwhile.
    nextFetch = millis() + DAY_MS;
    return true;
  }

  std::string body;
  bool fetched = false;
  if (saved_wifi::connect()) {
    if (ESP.getFreeHeap() >= HttpDownloader::MIN_TLS_FREE_HEAP &&
        ESP.getMaxAllocHeap() >= HttpDownloader::MIN_TLS_MAX_ALLOC) {
      fetched = HttpDownloader::fetchUrl(weather::forecastUrl(latitude, longitude, useFahrenheit), body);
    } else {
      LOG_ERR("WX", "Heap too low for TLS: free=%u max=%u", static_cast<unsigned>(ESP.getFreeHeap()),
              static_cast<unsigned>(ESP.getMaxAllocHeap()));
    }
  }
  saved_wifi::disconnect();

  WeatherForecast fresh;
  if (!fetched || !weather::parseResponse(body, latitude, longitude, useFahrenheit, fresh)) {
    LOG_ERR("WX", "Forecast update failed");
    setStatus(Status::Failed);
    if (++failures <= MAX_RETRIES) {
      nextFetch = millis() + RETRY_MS;
    } else {
      failures = 0;
      scheduleDaily();
    }
    return true;  // the status line changed
  }

  if (!weather::clockValid()) {
    // current.time is the start of a 15-minute slot: land mid-slot. Close
    // enough for the daily schedule and the date tile; no clock is shown.
    timeval now{static_cast<time_t>(fresh.observedAt + 450), 0};
    settimeofday(&now, nullptr);
    LOG_INF("WX", "System clock set from the forecast");
  }
  {
    RenderLock lock;
    current = fresh;
    state = Status::Ready;
  }
  JsonDocument doc;
  fresh.toJson(doc);
  if (!PersistableStoreBase::writeDocToFile(weather::CACHE_PATH, doc)) LOG_ERR("WX", "Failed to cache forecast");
  failures = 0;
  scheduleDaily();
  LOG_INF("WX", "Forecast updated: %ld, code %d", std::lround(fresh.temperature), fresh.code);
  return true;
}

void WeatherService::scheduleDaily() {
  if (!weather::clockValid()) {
    nextFetch = millis() + DAY_MS;
    return;
  }
  const tm local = weather::localAt(time(nullptr), current.utcOffset);
  const long secondsIntoDay = local.tm_hour * 3600L + local.tm_min * 60L + local.tm_sec;
  long wait = FETCH_HOUR * 3600L - secondsIntoDay;
  if (wait <= 60) wait += 24 * 3600L;
  nextFetch = millis() + static_cast<unsigned long>(wait) * 1000UL;
  LOG_DBG("WX", "Next forecast fetch in %ld s", wait);
}
