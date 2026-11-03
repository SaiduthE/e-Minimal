#include "SecuritySettingsActivity.h"

#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <iterator>

#include "activities/util/ConfirmationActivity.h"
#include "network/PhonePortal.h"

namespace {
constexpr SettingRowsActivity::Field FIELDS[] = {
    &CrossPointSettings::webAccessCode,     &CrossPointSettings::webAccessLevel,
    &CrossPointSettings::webDav,            &CrossPointSettings::networkAnnounce,
    &CrossPointSettings::hotspotPasswordMode, &CrossPointSettings::fileTransferIdleStop,
    &CrossPointSettings::backgroundOpenWifi,
};
}  // namespace

SecuritySettingsActivity::SecuritySettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : SettingRowsActivity("SecuritySettings", renderer, mappedInput, StrId::STR_SECURITY, FIELDS,
                          static_cast<int>(std::size(FIELDS)), StrId::STR_RESET_HOTSPOT_PASSWORD) {}

void SecuritySettingsActivity::onActionRow() {
  auto confirmation = makeUniqueNoThrow<ConfirmationActivity>(renderer, mappedInput, tr(STR_RESET_HOTSPOT_PASSWORD),
                                                              tr(STR_RESET_HOTSPOT_PASSWORD_BODY));
  if (!confirmation) {
    LOG_ERR("SECURITY", "OOM: reset confirmation");
    return;
  }
  startActivityForResult(std::move(confirmation), [this](const ActivityResult& result) {
    if (!result.isCancelled) PhonePortal::resetSharedPassphrase();
    requestUpdate();
  });
}
