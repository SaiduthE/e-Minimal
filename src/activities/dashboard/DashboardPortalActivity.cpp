#include "DashboardPortalActivity.h"

#include <ESPmDNS.h>
#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>

#include <cstdio>

#include "CrossPointState.h"
#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "activities/network/NetworkModeSelectionActivity.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/PhoneJoinPanel.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/CrossPointWebServer.h"
#include "util/TaskWatchdog.h"

namespace {
// APP_STATE.dashboardPortalMode values.
constexpr uint8_t PORTAL_MODE_JOIN = 0;
constexpr uint8_t PORTAL_MODE_HOTSPOT = 1;
constexpr uint8_t HOTSPOT_CHANNEL = 1;
constexpr uint8_t HOTSPOT_MAX_CLIENTS = 4;
// handleClient() calls per loop pass, as File Transfer: uploads need the
// throughput. The watchdog is fed every 32 and input read every 64.
constexpr int SERVER_BURST = 500;
}  // namespace

DashboardPortalActivity::DashboardPortalActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                 const Section section, const uint8_t layoutId)
    : Activity("DashboardPortal", renderer, mappedInput), section(section), layoutId(layoutId) {}

DashboardPortalActivity::~DashboardPortalActivity() = default;

std::string DashboardPortalActivity::pagePath(const Section section, const uint8_t layoutId) {
  const char* fragment = "";
  switch (section) {
    case Section::Layouts:
      fragment = "#layouts";
      break;
    case Section::NewLayout:
      fragment = "#layout-new";
      break;
    case Section::EditLayout: {
      char path[32];
      snprintf(path, sizeof(path), "/dashboards#layout-%u", static_cast<unsigned>(layoutId));
      return path;
    }
    case Section::Weather:
      fragment = "#weather";
      break;
    case Section::Todo:
      fragment = "#todo";
      break;
    case Section::Image:
      fragment = "#image";
      break;
    case Section::Date:
      fragment = "#date";
      break;
    case Section::Overview:
    default:
      break;
  }
  return std::string("/dashboards") + fragment;
}

bool DashboardPortalActivity::skipLoopDelay() { return webServer && webServer->isRunning(); }

bool DashboardPortalActivity::preventAutoSleep() { return webServer && webServer->isRunning(); }

void DashboardPortalActivity::onEnter() {
  Activity::onEnter();
  LOG_INF("DPORTAL", "Enter: free heap %u bytes, largest block %u", static_cast<unsigned>(ESP.getFreeHeap()),
          static_cast<unsigned>(ESP.getMaxAllocHeap()));

  {
    RenderLock lock;
    // Portrait like the rest of the UI; the Dashboard underneath may be landscape.
    previousOrientation = renderer.getOrientation();
    renderer.setOrientation(GfxRenderer::Orientation::Portrait);
    renderer.promoteNextRefresh(HalDisplay::HALF_REFRESH);
    phase = Phase::Choosing;
    onHotspot = false;
    pageUrl.clear();
    pageAlt.clear();
    networkName.clear();
  }
  radioUsed = false;

  // WiFi and the web server need the room more than the SD-font caches, which
  // rebuild on demand (same release as File Transfer).
  if (auto* fcm = renderer.getFontCacheManager()) {
    fcm->releaseSdFontCaches();
  }
  openChoice();
}

void DashboardPortalActivity::openChoice() {
  const NetworkMode initial =
      APP_STATE.dashboardPortalMode == PORTAL_MODE_HOTSPOT ? NetworkMode::CREATE_HOTSPOT : NetworkMode::JOIN_NETWORK;
  auto choice = makeUniqueNoThrow<NetworkModeSelectionActivity>(
      renderer, mappedInput, NetworkModeSelectionActivity::Choices::JoinOrHotspot, tr(STR_APP_DASHBOARD), initial);
  if (!choice) {
    LOG_ERR("DPORTAL", "OOM: network choice screen");
    fail("Network choice");
    return;
  }
  startActivityForResult(std::move(choice), [this](const ActivityResult& result) {
    if (result.isCancelled) {
      // Leaving before the radio came on needs no restart; onExit() decides.
      finish();
      return;
    }
    onModeChosen(std::get<NetworkModeResult>(result.data).mode == NetworkMode::CREATE_HOTSPOT);
  });
}

void DashboardPortalActivity::onModeChosen(const bool hotspot) {
  const uint8_t remembered = hotspot ? PORTAL_MODE_HOTSPOT : PORTAL_MODE_JOIN;
  if (APP_STATE.dashboardPortalMode != remembered) {
    APP_STATE.dashboardPortalMode = remembered;
    APP_STATE.saveToFile();
  }

  if (hotspot) {
    startHotspot();
    return;
  }

  // The standard WiFi screen, as File Transfer's Join a Network: saved
  // networks first, then scan, pick and password.
  LOG_DBG("DPORTAL", "Turning on WiFi (STA mode)");
  radioUsed = true;
  WiFi.mode(WIFI_STA);
  auto wifi = makeUniqueNoThrow<WifiSelectionActivity>(renderer, mappedInput);
  if (!wifi) {
    LOG_ERR("DPORTAL", "OOM: WiFi screen");
    fail("WiFi selection");
    return;
  }
  startActivityForResult(std::move(wifi), [this](const ActivityResult& result) {
    if (result.isCancelled) {
      openChoice();
      return;
    }
    const auto& joined = std::get<WifiResult>(result.data);
    onJoined(joined.ssid, joined.ip);
  });
}

void DashboardPortalActivity::startHotspot() {
  {
    RenderLock lock;
    phase = Phase::Starting;
  }
  requestUpdate();

  radioUsed = true;
  WiFi.persistent(false);
  PhonePortal::Config config;
  config.ssid = PhonePortal::SHARED_SSID;
  config.passphrase = PhonePortal::sharedPassphrase();
  config.channel = HOTSPOT_CHANNEL;
  config.maxClients = HOTSPOT_MAX_CLIENTS;
  config.hostname = PhonePortal::SHARED_HOSTNAME;
  if (!portal.begin(config)) {
    fail("Hotspot");
    return;
  }
  if (!startServer()) {
    fail("Web server");
    return;
  }
  showPage(portal.ip(), std::string(), true);
}

void DashboardPortalActivity::onJoined(const std::string& ssid, const std::string& ip) {
  LOG_INF("DPORTAL", "On %s at %s", ssid.c_str(), ip.c_str());
  MDNS.end();
  if (!MDNS.begin(PhonePortal::SHARED_HOSTNAME)) {
    LOG_DBG("DPORTAL", "mDNS failed to start");
  }
  if (!startServer()) {
    fail("Web server");
    return;
  }
  showPage(ip, ssid, false);
}

bool DashboardPortalActivity::startServer() {
  // Again right before the allocation: the screens since onEnter() can have
  // refilled the SD-font caches.
  if (auto* fcm = renderer.getFontCacheManager()) {
    fcm->releaseSdFontCaches();
  }
  LOG_DBG("DPORTAL", "Free heap before server alloc: %d bytes", ESP.getFreeHeap());

  webServer = makeUniqueNoThrow<CrossPointWebServer>();
  if (!webServer) {
    LOG_ERR("DPORTAL", "OOM: web server");
    return false;
  }
  webServer->begin();
  if (!webServer->isRunning()) {
    LOG_ERR("DPORTAL", "Web server did not start");
    webServer.reset();
    return false;
  }
  return true;
}

void DashboardPortalActivity::showPage(const std::string& ip, const std::string& ssid, const bool hotspot) {
  const std::string path = pagePath(section, layoutId);
  {
    RenderLock lock;
    onHotspot = hotspot;
    networkName = ssid;
    pageUrl = "http://" + ip + path;
    pageAlt = std::string(tr(STR_OR_HTTP_PREFIX)) + PhonePortal::SHARED_HOSTNAME + ".local" + path;
    phase = Phase::Running;
  }
  LOG_INF("DPORTAL", "Serving %s, free heap %u bytes", pageUrl.c_str(), static_cast<unsigned>(ESP.getFreeHeap()));
  requestUpdate();
}

void DashboardPortalActivity::fail(const char* what) {
  LOG_ERR("DPORTAL", "%s would not come up", what);
  portal.end();
  {
    RenderLock lock;
    phase = Phase::Error;
  }
  requestUpdate();
}

void DashboardPortalActivity::onExit() {
  Activity::onExit();
  LOG_INF("DPORTAL", "Leaving: free heap %u bytes, largest block %u", static_cast<unsigned>(ESP.getFreeHeap()),
          static_cast<unsigned>(ESP.getMaxAllocHeap()));

  // Closes an upload in flight, so no partial file stays on the card.
  if (webServer) webServer->stop();
  portal.end();  // no-op unless the hotspot was up

  // Before the restart, which deep-sleep entry turns into a no-op.
  renderer.setOrientation(previousOrientation);
  if (!radioUsed) return;
  MDNS.end();
  if (WiFi.getMode() & WIFI_MODE_STA) {
    WiFi.disconnect(false);
    delay(30);
  }
  silentRestart();
}

void DashboardPortalActivity::loop() {
  if (phase == Phase::Error) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
        mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      finish();
    }
    return;
  }
  if (phase != Phase::Running) return;

  if (onHotspot) portal.loop();

  if (webServer && webServer->isRunning()) {
    // HTTP header parsing can be slow; feed the watchdog before and during.
    resetTaskWatchdogIfSubscribed();
    for (int i = 0; i < SERVER_BURST && webServer->isRunning(); i++) {
      webServer->handleClient();
      if ((i & 0x1F) == 0x1F) {
        resetTaskWatchdogIfSubscribed();
      }
      if ((i & 0x3F) == 0x3F) {
        yield();
        // Pump input inside the burst so Back stays responsive.
        mappedInput.update(true);
        if (mappedInput.wasReleased(MappedInputManager::Button::Back) || mappedInput.wasHomeGesture()) {
          finish();
          return;
        }
      }
    }
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
  }
}

void DashboardPortalActivity::render(RenderLock&&) {
  // The choice and WiFi screens paint themselves.
  if (phase == Phase::Choosing) return;
  renderer.clearScreen();

  auto& theme = UITheme::getInstance();
  const auto& metrics = theme.getMetrics();
  const Rect screen = theme.getScreenSafeArea(renderer, true, false);
  GUI.drawHeader(renderer, Rect{screen.x, screen.y + metrics.topPadding, screen.width, metrics.headerHeight},
                 tr(STR_APP_DASHBOARD));

  const int top = screen.y + metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing * 2;
  const Rect bounds{screen.x, top, screen.width, screen.y + screen.height - top};

  switch (phase) {
    case Phase::Running: {
      PhoneJoinPanel::Content content;
      content.headline = tr(STR_DASH_PORTAL_HINT);
      content.pageUrl = pageUrl;
      content.pageAlt = pageAlt.c_str();
      char status[128];
      if (onHotspot) {
        // Step 1 joins the hotspot (its QR carries the passphrase); step 2
        // opens the page, since the captive popup opens "/" instead.
        content.portal = &portal;
      } else {
        snprintf(status, sizeof(status), tr(STR_DASH_PORTAL_SAME_NETWORK), networkName.c_str());
        content.status = status;
      }
      PhoneJoinPanel::draw(renderer, bounds, content);
      break;
    }
    case Phase::Error:
      UITheme::drawCenteredWrappedText(renderer, bounds, UI_12_FONT_ID, tr(STR_DASH_PORTAL_ERROR), 3, true,
                                       EpdFontFamily::BOLD);
      break;
    case Phase::Starting:
    default:
      UITheme::drawCenteredWrappedText(renderer, bounds, UI_12_FONT_ID, tr(STR_STARTING_HOTSPOT), 2);
      break;
  }

  const auto labels = mappedInput.mapLabels(tr(STR_EXIT), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
