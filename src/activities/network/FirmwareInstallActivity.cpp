#include "FirmwareInstallActivity.h"

#include <ESPmDNS.h>
#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>

#include <algorithm>
#include <cstdio>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "WifiSelectionActivity.h"
#include "activities/settings/SdFirmwareUpdateActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/PhonePortal.h"
#include "util/TaskWatchdog.h"

void FirmwareInstallActivity::onEnter() {
  Activity::onEnter();
  LOG_INF("FWI", "Free heap at onEnter: %d bytes", ESP.getFreeHeap());

  // Wi-Fi (~45 KB) and the server have to fit in what the reader leaves; the SD-font caches are rebuildable.
  if (auto* fcm = renderer.getFontCacheManager()) fcm->releaseSdFontCaches();

  state = State::JOINING;
  WiFi.mode(WIFI_STA);
  wifiStarted = true;
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) {
                           if (result.isCancelled) {
                             onGoHome();
                             return;
                           }
                           const auto& wifi = std::get<WifiResult>(result.data);
                           ssid = wifi.ssid;
                           ip = wifi.ip;
                           onWifiResult(true);
                         });
}

void FirmwareInstallActivity::onExit() {
  Activity::onExit();
  if (server) server->stop();
  server.reset();
  MDNS.end();
  if (wifiStarted) {
    WiFi.disconnect(false);
    delay(30);
    silentRestart();  // returns the heap the radio took
  }
}

void FirmwareInstallActivity::onWifiResult(const bool connected) {
  if (!connected) return;
  if (auto* fcm = renderer.getFontCacheManager()) fcm->releaseSdFontCaches();
  startServer();
}

void FirmwareInstallActivity::startServer() {
  server = makeUniqueNoThrow<FirmwareInstallServer>();
  if (!server) {
    LOG_ERR("FWI", "No memory for the install server");
    onGoHome();
    return;
  }
  const bool announce = SETTINGS.networkAnnounce != 0;
  if (!server->begin(announce)) {
    server.reset();
    onGoHome();
    return;
  }
  if (announce) {
    MDNS.end();
    announced = MDNS.begin(PhonePortal::SHARED_HOSTNAME);
    if (announced) MDNS.addService("eminimal-update", "tcp", 80);
  }
  state = State::WAITING;
  receiving = false;
  requestUpdate();
}

void FirmwareInstallActivity::installStaged() {
  // Free the radio and the server first: the flash pass needs the heap and a quiet SD bus. The install activity
  // restarts the device on success; on failure we restart here to get the Wi-Fi heap back.
  state = State::INSTALLING;
  server->stop();
  server.reset();
  MDNS.end();
  WiFi.disconnect(false);
  WiFi.mode(WIFI_OFF);
  LOG_INF("FWI", "Installing staged firmware, free heap %d bytes", ESP.getFreeHeap());

  startActivityForResult(
      std::make_unique<SdFirmwareUpdateActivity>(renderer, mappedInput, false, FirmwareInstallServer::STAGED_PATH),
      [](const ActivityResult&) {
        Storage.remove(FirmwareInstallServer::STAGED_PATH);
        silentRestart();
      });
}

void FirmwareInstallActivity::loop() {
  if (state != State::WAITING || !server || !server->isRunning()) return;

  resetTaskWatchdogIfSubscribed();
  constexpr int MAX_ITERATIONS = 500;
  for (int i = 0; i < MAX_ITERATIONS; i++) {
    server->handleClient();
    if ((i & 0x1F) == 0x1F) resetTaskWatchdogIfSubscribed();
    if ((i & 0x3F) == 0x3F) {
      yield();
      mappedInput.update(true);  // keep Back responsive inside this blocking loop
      if (mappedInput.wasReleased(MappedInputManager::Button::Back) || mappedInput.wasHomeGesture()) {
        onGoHome();
        return;
      }
    }
  }

  size_t stagedSize = 0;
  if (server->takeStaged(stagedSize)) {
    LOG_INF("FWI", "Staged firmware %u bytes, handing over to the install screen",
            static_cast<unsigned>(stagedSize));
    installStaged();
    return;
  }

  if (server->isReceiving() != receiving) {
    receiving = server->isReceiving();
    requestUpdate();
  }
  if (!receiving && millis() - server->lastActivityMs() > IDLE_STOP_MS) {
    LOG_INF("FWI", "No request for %lu s; leaving", IDLE_STOP_MS / 1000UL);
    onGoHome();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) || mappedInput.wasHomeGesture()) onGoHome();
}

void FirmwareInstallActivity::render(RenderLock&&) {
  if (state != State::WAITING) return;  // the Wi-Fi and install screens draw themselves

  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                 tr(STR_WEB_FIRMWARE_INSTALL), nullptr);
  GUI.drawSubHeader(renderer, Rect{0, metrics.topPadding + metrics.headerHeight, pageWidth, metrics.tabBarHeight},
                    ssid.c_str());

  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const int side = metrics.contentSidePadding;
  int y = metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight + metrics.verticalSpacing * 3;
  const auto drawLine = [&](const char* text, const EpdFontFamily::Style style = EpdFontFamily::REGULAR) {
    const Rect bounds{side, y, pageWidth - side * 2, pageHeight - y};
    UITheme::drawCenteredWrappedText(renderer, bounds, UI_10_FONT_ID, text, 3, true, style,
                                     UITheme::TextVerticalAlignment::TOP);
    const int lines = static_cast<int>(renderer.wrappedText(UI_10_FONT_ID, text, bounds.width, 3, style).size());
    y += lineHeight * std::max(1, lines) + metrics.verticalSpacing;
  };

  char line[96];
  drawLine(tr(STR_FWI_PC_HINT));
  y += metrics.verticalSpacing;
  snprintf(line, sizeof(line), tr(STR_FWI_ADDRESS_LINE), ip.c_str());
  drawLine(line, EpdFontFamily::BOLD);
  if (announced) {
    snprintf(line, sizeof(line), tr(STR_FWI_NAME_LINE), PhonePortal::SHARED_HOSTNAME);
    drawLine(line);
  }
  y += metrics.verticalSpacing;
  drawLine(receiving ? tr(STR_FWI_RECEIVING) : tr(STR_FWI_WAITING), EpdFontFamily::BOLD);
  drawLine(tr(STR_FWI_CONFIRM_NOTE));

  const auto labels = mappedInput.mapLabels(tr(STR_EXIT), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
