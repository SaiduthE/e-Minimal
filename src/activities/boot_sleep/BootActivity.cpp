#include "BootActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "fontIds.h"
#include "FirmwareIdentity.h"
#include "images/Logo120.h"

void BootActivity::onEnter() {
  Activity::onEnter();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  int marginTop, marginRight, marginBottom, marginLeft;
  renderer.getOrientedViewableTRBL(&marginTop, &marginRight, &marginBottom, &marginLeft);

  // Lines stack by their fonts' own line heights, so a board's larger font
  // tier moves them with it instead of overlapping or running off the panel.
  const int logoY = (pageHeight - 120) / 2;
  const int nameY = logoY + 120 + 10;
  const int bootingY = nameY + renderer.getLineHeight(UI_10_FONT_ID);
  // The version line keeps three empty lines of its own height below it.
  const int versionY = pageHeight - marginBottom - 4 * renderer.getLineHeight(SMALL_FONT_ID);

  renderer.clearScreen();
  renderer.drawImage(Logo120, (pageWidth - 120) / 2, logoY, 120, 120);
  renderer.drawCenteredText(UI_10_FONT_ID, nameY, FIRMWARE_NAME_TEXT, true, EpdFontFamily::BOLD);
  renderer.drawCenteredText(SMALL_FONT_ID, bootingY, tr(STR_BOOTING));
  renderer.drawCenteredText(SMALL_FONT_ID, versionY, FIRMWARE_DISPLAY_VERSION);
  renderer.displayBuffer();
}
