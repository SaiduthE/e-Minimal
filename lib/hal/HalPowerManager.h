#pragma once

#include <Arduino.h>
#include <BatteryMonitor.h>
#include <InputManager.h>
#include <Logging.h>
#include <freertos/semphr.h>

#include <cassert>

#include "HalGPIO.h"

class HalPowerManager;
extern HalPowerManager powerManager;  // Singleton

class HalPowerManager {
  int normalFreq = 0;  // MHz
  bool isLowPower = false;

  mutable int _batteryCachedPercent = 0;         // Last read battery percentage (0-100)
  mutable unsigned long _batteryLastPollMs = 0;  // Timestamp of last battery read in milliseconds

  enum LockMode { None, NormalSpeed };
  LockMode currentLockMode = None;
  SemaphoreHandle_t modeMutex = nullptr;  // Protect access to currentLockMode

 public:
#if BOARD_HAS_PSRAM
  static constexpr int LOW_POWER_FREQ = 80;  // MHz
#else
  static constexpr int LOW_POWER_FREQ = 10;  // MHz
#endif
  static constexpr unsigned long IDLE_POWER_SAVING_MS = 3000;  // ms
  static constexpr unsigned long BATTERY_POLL_MS = 1500;       // ms

  void begin();

  // Control CPU frequency for power saving
  void setPowerSaving(bool enabled);

  // Setup wake up GPIO and enter deep sleep
  // Should be called inside main loop() to handle the currentLockMode
  // timerWakeMs > 0 also wakes after that long (the Dashboard's next due widget).
  void startDeepSleep(HalGPIO& gpio, uint32_t timerWakeMs = 0) const;
  // Back to deep sleep after a wake that did not count (a power press too short
  // to verify), keeping the timer wake the last sleep had armed.
  void resumeDeepSleep(HalGPIO& gpio) const;

  // e-Minimal: a deep sleep also wakes when the USB-detect line (ext0) stops
  // matching the glass (the charging badge shown with the cable out, or not
  // shown with it in) and, while the badge is up, every CHARGE_TICK_MS to
  // refresh it.
#ifndef EMINIMAL_CHARGE_TICK_S
#define EMINIMAL_CHARGE_TICK_S 1800
#endif
  static constexpr uint32_t CHARGE_TICK_MS = static_cast<uint32_t>(EMINIMAL_CHARGE_TICK_S) * 1000UL;
  static bool badgeOnGlass();
  static void setBadgeOnGlass(bool shown);
  static bool chargeTickArmed();  // the timer armed was the badge's, not the caller's
  // Off: a sleep neither wakes on the USB-detect line nor arms the tick, so a
  // cable in or out leaves the sleep screen alone. Kept through deep sleep; the
  // caller sets it from its settings before each sleep.
  static void setChargeBadgeEnabled(bool enabled);
  // A USB-detect wake that found no change. After MAX_USB_BOUNCES in a row the
  // line is taken for noise (a divider sitting at the input threshold) and
  // sleeps stop waking on it until a user wake clears the count.
  static constexpr uint8_t MAX_USB_BOUNCES = 3;
  static void noteUsbBounce();
  static void clearUsbBounces();
  // A key pressed during a badge wake: the next sleep wakes at once, without
  // waiting for the key's release, and that boot takes it as a verified
  // power-button wake (takeWakeRequest()).
  static void requestWake();
  static bool takeWakeRequest();

  enum class LightSleepWake : uint8_t { Timer, Button, NotSupported };
  // Light sleep until maxMs passes (0: no timer) or any key goes down. RAM, the
  // running activity and the panel's image survive; the CPUs stop. For screens
  // that idle between timed updates. NotSupported, without sleeping, on boards
  // whose keys cannot wake it (an ADC ladder).
  LightSleepWake lightSleep(uint32_t maxMs) const;

  // Get battery percentage (range 0-100)
  uint16_t getBatteryPercentage() const;

  // RAII helper class to manage power saving locks
  // Usage: create an instance of Lock in a scope to disable power saving, for example when running a task that needs
  // full performance. When the Lock instance is destroyed (goes out of scope), power saving will be re-enabled.
  class Lock {
    friend class HalPowerManager;
    bool valid = false;

   public:
    explicit Lock();
    ~Lock();

    // Non-copyable and non-movable
    Lock(const Lock&) = delete;
    Lock& operator=(const Lock&) = delete;
    Lock(Lock&&) = delete;
    Lock& operator=(Lock&&) = delete;
  };
};
