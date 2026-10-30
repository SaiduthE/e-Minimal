#include "SleepScreenSettingsActivity.h"

#include <iterator>

namespace {
constexpr SettingRowsActivity::Field FIELDS[] = {
    &CrossPointSettings::sleepScreen,
    &CrossPointSettings::sleepScreenCoverMode,
    &CrossPointSettings::sleepScreenCoverFilter,
    &CrossPointSettings::quickResumeSleepScreen,
};
}  // namespace

SleepScreenSettingsActivity::SleepScreenSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : SettingRowsActivity("SleepScreenSettings", renderer, mappedInput, StrId::STR_SLEEP_SCREEN, FIELDS,
                          static_cast<int>(std::size(FIELDS))) {}

void SleepScreenSettingsActivity::onEnter() {
  SettingRowsActivity::onEnter();
  preserveQuickResumeTimeoutOn =
      SETTINGS.quickResumeSleepScreen == CrossPointSettings::QUICK_RESUME_SLEEP_SCREEN::QUICK_RESUME_AFTER_TIMEOUT;
  quickResumeTimeoutAutoEnabled = false;
}

// Same rules as SettingsActivity::syncQuickResumeTimeoutForSleepScreen().
void SleepScreenSettingsActivity::onSettingChanged(const Field field) {
  const bool sleepScreenChanged = field == &CrossPointSettings::sleepScreen;
  if (field == &CrossPointSettings::quickResumeSleepScreen) {
    preserveQuickResumeTimeoutOn =
        SETTINGS.quickResumeSleepScreen == CrossPointSettings::QUICK_RESUME_SLEEP_SCREEN::QUICK_RESUME_AFTER_TIMEOUT;
    quickResumeTimeoutAutoEnabled = false;
  }

  if (SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::QUICK_RESUME) {
    if (SETTINGS.quickResumeSleepScreen != CrossPointSettings::QUICK_RESUME_SLEEP_SCREEN::QUICK_RESUME_AFTER_TIMEOUT) {
      SETTINGS.quickResumeSleepScreen = CrossPointSettings::QUICK_RESUME_SLEEP_SCREEN::QUICK_RESUME_AFTER_TIMEOUT;
      quickResumeTimeoutAutoEnabled = !preserveQuickResumeTimeoutOn;
    } else if (sleepScreenChanged && !preserveQuickResumeTimeoutOn) {
      quickResumeTimeoutAutoEnabled = true;
    }
    return;
  }

  if (sleepScreenChanged && quickResumeTimeoutAutoEnabled && !preserveQuickResumeTimeoutOn) {
    SETTINGS.quickResumeSleepScreen = CrossPointSettings::QUICK_RESUME_SLEEP_SCREEN::QUICK_RESUME_NEVER;
    quickResumeTimeoutAutoEnabled = false;
  }
}
