#pragma once

class MappedInputManager;

// The dashboard's standby for its screens that have no timer of their own (the
// to-do list): after IDLE_MS without a key it light-sleeps until a key goes
// down, the panel still showing the screen, and the waking key only wakes. The
// timed screens have the same behaviour in TimedDashboardActivity. Owners
// return true from preventAutoSleep(): this replaces the deep auto-sleep.
class IdleStandby {
 public:
  static constexpr unsigned long IDLE_MS = 30000;

  // On entry.
  void reset();
  // First thing in loop(). busy: an interaction is open (a popup). True when
  // this pass is consumed: the waking key's press or release, or a wake.
  bool poll(const MappedInputManager& input, bool busy);

 private:
  // A longer gap between passes means another screen covered this one.
  static constexpr unsigned long COVERED_GAP_MS = 1000;

  unsigned long lastActivity = 0;
  unsigned long lastPoll = 0;
  bool swallowKeys = false;
  bool unsupported = false;
};
