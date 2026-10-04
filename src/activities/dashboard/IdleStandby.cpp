#include "IdleStandby.h"

#include <Arduino.h>
#include <HalGPIO.h>
#include <HalPowerManager.h>
#include <Logging.h>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "activities/Activity.h"
#include "activities/ActivityManager.h"
#include "activities/RenderLock.h"
#include "util/TaskWatchdog.h"

namespace {
bool anyKeyDown(const MappedInputManager& input) {
  using Button = MappedInputManager::Button;
  return input.isPressed(Button::Back) || input.isPressed(Button::Confirm) || input.isPressed(Button::Up) ||
         input.isPressed(Button::Down) || input.isPressed(Button::Left) || input.isPressed(Button::Right);
}
}  // namespace

void IdleStandby::reset() {
  lastActivity = millis();
  lastPoll = lastActivity;
  swallowKeys = false;
}

bool IdleStandby::poll(const MappedInputManager& input, const bool busy) {
  const unsigned long now = millis();
  if (now - lastPoll > COVERED_GAP_MS) lastActivity = now;
  lastPoll = now;

  if (swallowKeys) {
    // The key that ended standby: wait out its hold, then drop the pass that
    // carries its release.
    if (anyKeyDown(input)) return true;
    swallowKeys = false;
    lastActivity = now;
    return true;
  }
  if (busy || input.wasAnyPressed() || anyKeyDown(input)) {
    lastActivity = now;
    return false;
  }
  if (unsupported || now - lastActivity < IDLE_MS) return false;

  // Past Time to Sleep, deep sleep with the screen left on the glass
  // (enterDeepSleep); until then light sleep, capped at that deadline.
  const unsigned long sleepAfter = SETTINGS.getSleepTimeoutMs();
  const unsigned long idle = now - lastActivity;
  if (sleepAfter > 0 && idle >= sleepAfter) {
    activityManager.requestDeepSleep();
    return true;
  }
  // No light standby with a USB host: it drops the console (as in the reader).
  if (gpio.isUsbConnected() || logSerial) return false;
  const unsigned long capMs = sleepAfter > 0 ? sleepAfter - idle : 0;

  LOG_DBG("DASH", "Idle standby until a key");
  HalPowerManager::LightSleepWake wake;
  {
    // Holding the render lock keeps a repaint from being frozen halfway.
    RenderLock lock;
    resetTaskWatchdogIfSubscribed();
    wake = powerManager.lightSleep(capMs);
    resetTaskWatchdogIfSubscribed();
  }
  lastPoll = millis();
  if (wake == HalPowerManager::LightSleepWake::NotSupported) {
    unsupported = true;
    return false;
  }
  // The deadline's timer: the next poll deep-sleeps.
  if (wake == HalPowerManager::LightSleepWake::Timer) return true;
  lastActivity = lastPoll;
  swallowKeys = true;
  return true;
}
