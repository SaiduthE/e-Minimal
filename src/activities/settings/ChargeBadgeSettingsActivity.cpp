#include "ChargeBadgeSettingsActivity.h"

#include <iterator>

namespace {
constexpr SettingRowsActivity::Field FIELDS[] = {
    &CrossPointSettings::chargeBadgeEnabled,  &CrossPointSettings::chargeBadgeStyle,
    &CrossPointSettings::chargeBadgeSize,     &CrossPointSettings::chargeBadgePosition,
    &CrossPointSettings::chargeBadgeInverted,
};
}  // namespace

ChargeBadgeSettingsActivity::ChargeBadgeSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : SettingRowsActivity("ChargeBadgeSettings", renderer, mappedInput, StrId::STR_CHARGE_BADGE, FIELDS,
                          static_cast<int>(std::size(FIELDS))) {}
