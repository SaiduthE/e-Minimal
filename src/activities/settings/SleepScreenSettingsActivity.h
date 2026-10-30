#pragma once

#include "SettingRowsActivity.h"

// Global Settings > Power & Sleep > Sleep Screen: the sleep image and how it is
// made.
class SleepScreenSettingsActivity final : public SettingRowsActivity {
 public:
  SleepScreenSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;

 private:
  void onSettingChanged(Field field) override;

  // The Quick Resume sleep screen needs Quick Resume on Timeout; this page turns
  // it on for that screen and restores what the user had when they leave it.
  bool preserveQuickResumeTimeoutOn = false;
  bool quickResumeTimeoutAutoEnabled = false;
};
