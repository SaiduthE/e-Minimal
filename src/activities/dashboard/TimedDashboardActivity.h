#pragma once

#include <atomic>
#include <cstdint>

#include "activities/Activity.h"

// Base for dashboard screens that update on a timer (the tiled dashboard, the
// full forecast, the full-screen image). While one is on screen the device
// does not auto-sleep. After IDLE_BEFORE_STANDBY_MS with no key and no repaint
// in flight it light-sleeps (HalPowerManager::lightSleep) until the next update
// is due or a key goes down; the panel keeps the dashboard. The key that ends a
// standby only wakes: its press and release are swallowed. Power menu Sleep is
// the normal deep sleep.
class TimedDashboardActivity : public Activity {
 public:
  // Subclasses call this first: it starts the idle countdown.
  void onEnter() override;
  void loop() final;
  void render(RenderLock&& lock) final;
  void requestUpdate(bool immediate = false) override;
  bool preventAutoSleep() override { return true; }

 protected:
  TimedDashboardActivity(const char* name, GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity(name, renderer, mappedInput) {}

  // millis() at which update() should next run; 0 when nothing is scheduled.
  virtual unsigned long nextUpdateMs() const = 0;
  // Runs on the loop task once nextUpdateMs() has passed, awake or after a
  // timer wake. Calls requestUpdate() when the screen changes.
  virtual void update() = 0;
  // Keys the base did not consume (it takes Back). Return true when handled.
  virtual bool handleKeys() { return false; }
  // Back: pops back to the screen that opened this one.
  virtual void onBack() { finish(); }
  // Idle and about to sleep. Return true after requesting a repaint that must
  // land first (the tiles drop their focus frame); the base waits for it.
  virtual bool prepareStandby() { return false; }
  // A standby ended, by a key or by the timer (whose due update already ran).
  virtual void onWake(bool byKey) {}
  // Paints the frame; the base tracks when the requested repaints are done.
  virtual void draw(RenderLock&& lock) = 0;

  // Restarts the idle countdown before standby.
  void noteActivity() { lastActivityMs = millis(); }
  // A requested repaint has not finished yet.
  bool repaintPending() const { return repaintsDone.load() != repaintsRequested.load(); }

 private:
  // After entry, a key or any wake.
  static constexpr unsigned long IDLE_BEFORE_STANDBY_MS = 30000;
  // Not worth sleeping for less: the loop picks the update up awake.
  static constexpr unsigned long MIN_STANDBY_MS = 2000;
  // A longer gap between loop passes means something covered this screen (the
  // power menu, a view it opened): restart the idle countdown so the repaint
  // after the cover closes lands before standby.
  static constexpr unsigned long COVERED_GAP_MS = 1000;

  bool anyKeyDown() const;
  void standby();

  unsigned long lastActivityMs = 0;
  unsigned long lastLoopMs = 0;
  bool swallowKeys = false;
  bool standbyUnsupported = false;
  // prepareStandby() ran for the idle stretch now ending in standby.
  bool standbyPrepared = false;
  // Repaints requested and finished: standby waits until they match, so the
  // panel is not left mid-update.
  std::atomic<uint32_t> repaintsRequested{0};
  std::atomic<uint32_t> repaintsDone{0};
};
