#pragma once

#include "SettingRowsActivity.h"

// Global Settings > Display & System > Security: who can reach File Transfer and
// the Wi-Fi hotspot, and what background Wi-Fi may join.
class SecuritySettingsActivity final : public SettingRowsActivity {
 public:
  SecuritySettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

 protected:
  void onActionRow() override;
};
