#include "WebSession.h"

#include <Arduino.h>
#include <esp_random.h>

#include <cstdio>

#include "CrossPointSettings.h"

namespace {
// Security > Auto-stop File Transfer: Off / 5 / 15 / 30 min, by stored index.
constexpr uint32_t IDLE_STOP_MINUTES[] = {0, 5, 15, 30};
}  // namespace

void WebSession::begin() {
  uint8_t raw[16];
  esp_fill_random(raw, sizeof(raw));
  for (size_t i = 0; i < sizeof(raw); i++) {
    snprintf(token + i * 2, 3, "%02x", raw[i]);
  }
  snprintf(codeText, sizeof(codeText), "%06u", static_cast<unsigned>(esp_random() % 1000000U));

  options.requireCode = SETTINGS.webAccessCode != 0;
  options.accessLevel = SETTINGS.webAccessLevel <= 2 ? SETTINGS.webAccessLevel : 0;
  options.allowFirmware = SETTINGS.webFirmwareInstall != 0;
  options.webdav = SETTINGS.webDav != 0;
  options.discovery = SETTINGS.networkAnnounce != 0;

  const uint8_t stop = SETTINGS.fileTransferIdleStop;
  idleLimitMs = stop < sizeof(IDLE_STOP_MINUTES) / sizeof(IDLE_STOP_MINUTES[0])
                    ? IDLE_STOP_MINUTES[stop] * 60UL * 1000UL
                    : 0;
  startedMs = millis();
}

void WebSession::applyTo(CrossPointWebServer& server) const { server.setSecurity(options, token, codeText); }

std::string WebSession::urlWithToken(const std::string& url) const {
  if (!options.requireCode) return url;
  const size_t fragment = url.find('#');
  std::string out = url.substr(0, fragment);
  out += out.find('?') == std::string::npos ? "?t=" : "&t=";
  out += token;
  if (fragment != std::string::npos) out += url.substr(fragment);
  return out;
}

bool WebSession::idleExpired(const CrossPointWebServer& server) const {
  if (idleLimitMs == 0) return false;
  const uint32_t last = server.lastActivityMs();
  const uint32_t reference = static_cast<int32_t>(last - startedMs) > 0 ? last : startedMs;
  return millis() - reference >= idleLimitMs;
}
