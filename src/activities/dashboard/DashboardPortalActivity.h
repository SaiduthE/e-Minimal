#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "activities/Activity.h"
#include "network/PhonePortal.h"

class CrossPointWebServer;

// Every Dashboard setting is edited on the Dashboards page of the e-Minimal web
// UI; this brings that server up and shows the QR codes that open it. First
// the same choice File Transfer offers: Join a Network (the standard WiFi
// screen; the phone must be on that network too) or Create Hotspot (the shared
// one). Once the radio has been up, leaving restarts silently, like File
// Transfer; boot routing returns to the Dashboard app.
class DashboardPortalActivity final : public Activity {
 public:
  // EditLayout takes the layout's id.
  enum class Section : uint8_t { Overview, Layouts, NewLayout, EditLayout, Weather, Todo, Image, Date };

  DashboardPortalActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, Section section = Section::Overview,
                          uint8_t layoutId = 0);
  ~DashboardPortalActivity() override;

  // "/dashboards" plus the section's fragment: "", "#layouts", "#layout-new",
  // "#layout-<id>", "#weather", "#todo", "#image", "#date".
  static std::string pagePath(Section section, uint8_t layoutId);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  // HTTP (and DNS on the hotspot) are answered from loop().
  bool skipLoopDelay() override;
  bool preventAutoSleep() override;

 private:
  // Choosing: the choice or WiFi screen is on top; this one paints nothing.
  enum class Phase : uint8_t { Choosing, Starting, Running, Error };

  void openChoice();
  void onModeChosen(bool hotspot);
  void startHotspot();
  void onJoined(const std::string& ssid, const std::string& ip);
  bool startServer();
  void showPage(const std::string& ip, const std::string& ssid, bool hotspot);
  void fail(const char* what);

  Section section;
  uint8_t layoutId;
  GfxRenderer::Orientation previousOrientation = GfxRenderer::Orientation::Portrait;

  std::unique_ptr<CrossPointWebServer> webServer;
  PhonePortal portal;
  Phase phase = Phase::Choosing;
  // The radio came on: leaving restarts.
  bool radioUsed = false;
  // Written under a RenderLock; render() reads them.
  bool onHotspot = false;
  std::string pageUrl;
  std::string pageAlt;
  std::string networkName;
};
