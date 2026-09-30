#include "IdleStandby.h"

#include <Arduino.h>
#include <HalPowerManager.h>
#include <Logging.h>

#include "MappedInputManager.h"
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

  LOG_DBG("DASH", "Idle standby until a key");
  HalPowerManager::LightSleepWake wake;
  {
    // Holding the render lock keeps a repaint from being frozen halfway.
    RenderLock lock;
    resetTaskWatchdogIfSubscribed();
    wake = powerManager.lightSleep(0);
    resetTaskWatchdogIfSubscribed();
  }
  lastPoll = millis();
  lastActivity = lastPoll;
  if (wake == HalPowerManager::LightSleepWake::NotSupported) {
    unsupported = true;
    return false;
  }
  swallowKeys = true;
  return true;
}
