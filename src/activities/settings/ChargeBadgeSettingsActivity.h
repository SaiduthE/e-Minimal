#pragma once

#include "SettingRowsActivity.h"

// Global Settings > Power & Sleep > Charge Badge: e-Minimal's charging badge
// over the sleep screen.
class ChargeBadgeSettingsActivity final : public SettingRowsActivity {
 public:
  ChargeBadgeSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);
};
