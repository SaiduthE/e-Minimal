#include "TimedDashboardActivity.h"

#include <HalPowerManager.h>
#include <Logging.h>

#include "MappedInputManager.h"
#include "util/TaskWatchdog.h"

void TimedDashboardActivity::onEnter() {
  Activity::onEnter();
  swallowKeys = false;
  standbyPrepared = false;
  noteActivity();
  lastLoopMs = millis();
}

void TimedDashboardActivity::requestUpdate(const bool immediate) {
  repaintsRequested.fetch_add(1);
  Activity::requestUpdate(immediate);
}

void TimedDashboardActivity::render(RenderLock&& lock) {
  const uint32_t requested = repaintsRequested.load();
  draw(std::move(lock));
  repaintsDone.store(requested);
}

bool TimedDashboardActivity::anyKeyDown() const {
  using Button = MappedInputManager::Button;
  return mappedInput.isPressed(Button::Back) || mappedInput.isPressed(Button::Confirm) ||
         mappedInput.isPressed(Button::Up) || mappedInput.isPressed(Button::Down) ||
         mappedInput.isPressed(Button::Left) || mappedInput.isPressed(Button::Right);
}

void TimedDashboardActivity::loop() {
  if (millis() - lastLoopMs > COVERED_GAP_MS) noteActivity();
  lastLoopMs = millis();

  if (swallowKeys) {
    // The key that ended standby: wait out its hold, then drop the pass that
    // carries its release.
    if (anyKeyDown()) return;
    swallowKeys = false;
    noteActivity();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    onBack();
    return;
  }
  if (handleKeys() || mappedInput.wasAnyPressed()) {
    noteActivity();
    standbyPrepared = false;
  }

  const unsigned long due = nextUpdateMs();
  if (due != 0 && static_cast<long>(millis() - due) >= 0) update();

  if (standbyUnsupported || repaintsDone.load() != repaintsRequested.load() ||
      millis() - lastActivityMs < IDLE_BEFORE_STANDBY_MS) {
    return;
  }
  if (!standbyPrepared) {
    standbyPrepared = true;
    // Its repaint lands before a later pass sleeps.
    if (prepareStandby()) return;
  }
  standby();
}

void TimedDashboardActivity::standby() {
  const unsigned long due = nextUpdateMs();
  uint32_t sleepMs = 0;  // key wake only
  if (due != 0) {
    const long remaining = static_cast<long>(due - millis());
    if (remaining < static_cast<long>(MIN_STANDBY_MS)) return;
    sleepMs = static_cast<uint32_t>(remaining);
  }

  LOG_DBG("DASH", "%s standby for %lu ms", name.c_str(), static_cast<unsigned long>(sleepMs));
  HalPowerManager::LightSleepWake wake;
  {
    // Holding the render lock keeps the render task from starting a repaint
    // that the sleep would freeze halfway.
    RenderLock lock(*this);
    resetTaskWatchdogIfSubscribed();
    wake = powerManager.lightSleep(sleepMs);
    resetTaskWatchdogIfSubscribed();
  }
  lastLoopMs = millis();

  if (wake == HalPowerManager::LightSleepWake::NotSupported) {
    LOG_DBG("DASH", "Light sleep unsupported on this board; staying awake");
    standbyUnsupported = true;
    // Undo what prepareStandby() hid: this screen never sleeps now.
    onWake(false);
    return;
  }
  standbyPrepared = false;
  noteActivity();
  const bool byKey = wake == HalPowerManager::LightSleepWake::Button;
  if (byKey) {
    LOG_DBG("DASH", "%s woken by a key", name.c_str());
    swallowKeys = true;
  } else {
    // Run what woke it now, so onWake() can fold its repaint into the update's.
    const unsigned long next = nextUpdateMs();
    if (next != 0 && static_cast<long>(millis() - next) >= 0) update();
  }
  onWake(byKey);
}
