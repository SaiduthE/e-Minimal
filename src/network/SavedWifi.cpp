#include "SavedWifi.h"

#include <Arduino.h>
#include <Logging.h>
#include <WiFi.h>
#include <esp_mac.h>

#include <algorithm>
#include <string>
#include <vector>

#include "WifiCredentialStore.h"
#include "util/TaskWatchdog.h"

namespace saved_wifi {
namespace {

bool radioUsed = false;

void prepareStation() {
  WiFi.persistent(false);  // Credentials live in WifiCredentialStore, not the SDK's NVS
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true, true);
  delay(100);
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);

  // Same hostname as the WiFi screen gives the device.
  uint8_t mac[6] = {};
  if (esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK) {
    char hostname[sizeof("eMinimal-") + 12];
    snprintf(hostname, sizeof(hostname), "eMinimal-%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4],
             mac[5]);
    WiFi.setHostname(hostname);
  }
}

bool tryNetwork(const WifiCredential& credential, const unsigned long timeoutMs) {
  LOG_DBG("SWIFI", "Connecting to %s", credential.ssid.c_str());
  prepareStation();
  if (credential.password.empty()) {
    WiFi.begin(credential.ssid.c_str());
  } else {
    WiFi.begin(credential.ssid.c_str(), credential.password.c_str());
  }
  const unsigned long start = millis();
  while (millis() - start < timeoutMs) {
    const wl_status_t status = WiFi.status();
    if (status == WL_CONNECTED) {
      LOG_INF("SWIFI", "Connected to %s", credential.ssid.c_str());
      return true;
    }
    if (status == WL_CONNECT_FAILED) break;
    resetTaskWatchdogIfSubscribed();
    delay(100);
  }
  LOG_DBG("SWIFI", "No connection to %s", credential.ssid.c_str());
  return false;
}

}  // namespace

bool hasSavedNetworks() {
  WIFI_STORE.loadFromFile();
  return WIFI_STORE.getCredentialCount() > 0;
}

bool connect(const unsigned long perNetworkTimeoutMs) {
  if (!hasSavedNetworks()) {
    LOG_DBG("SWIFI", "No saved networks");
    return false;
  }
  radioUsed = true;

  const std::string lastSsid = WIFI_STORE.getLastConnectedSsid();
  if (!lastSsid.empty()) {
    const auto credential = WIFI_STORE.findCredential(lastSsid);
    if (credential && tryNetwork(*credential, perNetworkTimeoutMs)) return true;
  }

  // Then the other saved networks that are in range, strongest first.
  prepareStation();
  const int16_t found = WiFi.scanNetworks();
  resetTaskWatchdogIfSubscribed();
  struct Candidate {
    WifiCredential credential;
    int32_t rssi;
  };
  std::vector<Candidate> candidates;
  candidates.reserve(WIFI_STORE.getCredentialCount());
  for (int16_t i = 0; i < found; i++) {
    const std::string ssid = WiFi.SSID(i).c_str();
    if (ssid == lastSsid) continue;
    const bool seen = std::any_of(candidates.begin(), candidates.end(),
                                  [&ssid](const Candidate& c) { return c.credential.ssid == ssid; });
    if (seen) continue;
    auto credential = WIFI_STORE.findCredential(ssid);
    if (credential) candidates.push_back({std::move(*credential), WiFi.RSSI(i)});
  }
  WiFi.scanDelete();
  std::sort(candidates.begin(), candidates.end(),
            [](const Candidate& a, const Candidate& b) { return a.rssi > b.rssi; });
  for (const auto& candidate : candidates) {
    if (tryNetwork(candidate.credential, perNetworkTimeoutMs)) return true;
  }
  disconnect();
  return false;
}

bool usedSinceBoot() { return radioUsed; }

void disconnect() {
  WiFi.disconnect(true);
  delay(30);
  WiFi.mode(WIFI_OFF);
}

}  // namespace saved_wifi
