#pragma once
#include <ArduinoJson.h>
#include <PersistableStore.h>

#include <cstdint>
#include <string>

// The top-level apps the power menu switches between. The one last opened is
// the one the device boots or wakes into.
enum class AppId : uint8_t { READER = 0, DASHBOARD = 1, GAMES = 2 };

class CrossPointState : public PersistableStore<CrossPointState> {
  CrossPointState() = default;

  friend class PersistableStore<CrossPointState>;

 public:
  static constexpr uint8_t SLEEP_RECENT_COUNT = 16;

  std::string openEpubPath;
  uint16_t recentSleepImages[SLEEP_RECENT_COUNT] = {};
  uint8_t recentSleepPos = 0;
  uint8_t recentSleepFill = 0;
  uint16_t recentOverlaySleepImages[SLEEP_RECENT_COUNT] = {};
  uint8_t recentOverlaySleepPos = 0;
  uint8_t recentOverlaySleepFill = 0;
  uint8_t readerActivityLoadCount = 0;
  bool lastSleepFromReader = false;
  bool showBootScreen = true;
  AppId lastApp = AppId::READER;
  // Set when the reader app is left for another app: whether a book was on
  // screen, so switching back resumes it instead of opening Home.
  bool resumeBookOnReturn = false;
  // The dashboard Image widget's position in its folder's sorted file list.
  uint16_t carouselIndex = 0;
  // The Dashboard portal's last network choice: 0 Join a Network, 1 Create
  // Hotspot. Pre-selected the next time the portal opens.
  uint8_t dashboardPortalMode = 0;

  static const char* getFilePath() { return "/.crosspoint/state.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

  bool isRecentSleep(uint16_t idx, uint8_t checkCount) const;
  bool isRecentOverlaySleep(uint16_t idx, uint8_t checkCount) const;

  void pushRecentSleep(uint16_t idx);
  void pushRecentOverlaySleep(uint16_t idx);
};

#define APP_STATE CrossPointState::getInstance()
