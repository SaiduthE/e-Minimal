#pragma once

#include <cstdint>

#include "CrossPointSettings.h"
#include "activities/UiListActivity.h"

// Which way the launched dashboard turns (SETTINGS.dashboardOrientation), each
// choice pictured as the device held that way with its keys on the left, the
// right or at the bottom. The Dashboard home and its screens stay portrait.
// Select saves the choice and returns.
class OrientationPickerActivity final : public UiListActivity {
 public:
  explicit OrientationPickerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiListActivity("DashboardOrientation", renderer, mappedInput) {}

  void onEnter() override;

  // The name of a CrossPointSettings::DASHBOARD_ORIENTATION value.
  static const char* orientationName(uint8_t orientation);

 private:
  // Rows follow the setting's values.
  static constexpr int OPTION_COUNT = CrossPointSettings::DASHBOARD_ORIENTATION_COUNT;

  int listCount() const override { return OPTION_COUNT; }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;

  freeink::ui::ListItem rowItems_[OPTION_COUNT]{};
};
