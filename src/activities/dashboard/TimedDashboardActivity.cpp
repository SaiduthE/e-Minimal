#include "TimedDashboardActivity.h"

#include <HalGPIO.h>
#include <HalPowerManager.h>
#include <Logging.h>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "activities/ActivityManager.h"
#include "util/TaskWatchdog.h"

void TimedDashboardActivity::onEnter() {
  Activity::onEnter();
  swallowKeys = false;
  standbyPrepared = false;
  noteActivity();
  lastLoopMs = millis();
}

unsigned long TimedDashboardActivity::deepSleepWakeInMs() const {
  const unsigned long due = nextUpdateMs();
  if (due == 0) return 0;
  const long left = static_cast<long>(due - millis());
  return left > 1000 ? static_cast<unsigned long>(left) : 1000;
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
    timerWake = false;
    onBack();
    return;
  }
  if (handleKeys() || mappedInput.wasAnyPressed()) {
    noteActivity();
    standbyPrepared = false;
    timerWake = false;  // a key takes over from the timer wake
  }

  const unsigned long due = nextUpdateMs();
  if (due != 0 && static_cast<long>(millis() - due) >= 0) update();

  if (standbyUnsupported || repaintsDone.load() != repaintsRequested.load()) return;
  if (timerWake) {
    timerWake = false;
    LOG_DBG("DASH", "%s painted after a timer wake, sleeping again", name.c_str());
    activityManager.requestDeepSleep();
    return;
  }
  if (millis() - lastActivityMs < IDLE_BEFORE_STANDBY_MS) return;
  // Past Time to Sleep with no key, deep sleep: the screen stays on the glass
  // (enterDeepSleep) and a timer wakes the device when a widget is due.
  const unsigned long sleepAfter = SETTINGS.getSleepTimeoutMs();
  const unsigned long userIdle = millis() - lastUserMs;
  const bool deepSleepDue = sleepAfter > 0 && userIdle >= sleepAfter;
  // No light standby with a USB host: it drops the console, and on USB the
  // battery does not count. Deep sleep still comes at Time to Sleep.
  if (!deepSleepDue && (gpio.isUsbConnected() || logSerial)) return;
  if (!standbyPrepared) {
    standbyPrepared = true;
    // Its repaint lands before a later pass sleeps.
    if (prepareStandby()) return;
  }
  if (deepSleepDue) {
    LOG_DBG("DASH", "%s idle %lu s, deep sleep", name.c_str(), userIdle / 1000);
    activityManager.requestDeepSleep();
    return;
  }
  standby(sleepAfter > 0 ? sleepAfter - userIdle : 0);
}

void TimedDashboardActivity::standby(const unsigned long capMs) {
  const unsigned long due = nextUpdateMs();
  uint32_t sleepMs = 0;  // key wake only
  if (due != 0) {
    const long remaining = static_cast<long>(due - millis());
    if (remaining < static_cast<long>(MIN_STANDBY_MS)) return;
    sleepMs = static_cast<uint32_t>(remaining);
  }
  // The deep-sleep deadline comes first: that timer wake only leads to it.
  bool forDeadline = false;
  if (capMs > 0 && (sleepMs == 0 || capMs < sleepMs)) {
    if (capMs < MIN_STANDBY_MS) return;
    sleepMs = static_cast<uint32_t>(capMs);
    forDeadline = true;
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
  const bool byKey = wake == HalPowerManager::LightSleepWake::Button;
  // The deadline's timer: the next pass deep-sleeps, the screen as it is.
  if (!byKey && forDeadline) return;
  standbyPrepared = false;
  if (byKey) {
    LOG_DBG("DASH", "%s woken by a key", name.c_str());
    noteActivity();
    swallowKeys = true;
  } else {
    // A widget's timer: restart the standby countdown, not Time to Sleep.
    lastActivityMs = millis();
    // Run what woke it now, so onWake() can fold its repaint into the update's.
    const unsigned long next = nextUpdateMs();
    if (next != 0 && static_cast<long>(millis() - next) >= 0) update();
  }
  onWake(byKey);
}
