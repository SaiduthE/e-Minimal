#pragma once

#include <cstdint>
#include <string>

#include "network/CrossPointWebServer.h"

// One File Transfer / phone-page session's security: the access token the QR
// carries, the 6-digit code typed on the phone, and the Security settings the
// web server enforces. begin() snapshots the settings, so changing them takes
// effect on the next session.
class WebSession {
 public:
  // Call once Wi-Fi is up (esp_fill_random is only a true RNG with the radio
  // on) and before CrossPointWebServer::begin().
  void begin();
  void applyTo(CrossPointWebServer& server) const;

  // url + "?t=<token>" ("&t=" when the url already has a query), placed before
  // any #fragment. Unchanged when the access code is off.
  std::string urlWithToken(const std::string& url) const;

  bool requiresCode() const { return options.requireCode; }
  const char* code() const { return codeText; }
  // mDNS name and UDP discovery (Security > Announce on Network).
  bool announces() const { return options.discovery; }

  // True once the session has had no authenticated traffic for the configured
  // Auto-stop time (never when it is Off).
  bool idleExpired(const CrossPointWebServer& server) const;

 private:
  WebSecurityOptions options;
  char token[33] = "";
  char codeText[7] = "";
  uint32_t startedMs = 0;
  uint32_t idleLimitMs = 0;  // 0 = never
};
