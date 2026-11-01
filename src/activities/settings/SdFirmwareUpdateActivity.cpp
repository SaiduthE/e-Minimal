#include "SdFirmwareUpdateActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <esp_ota_ops.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "MappedInputManager.h"
#include "activities/home/FileBrowserActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/FirmwareFlasher.h"

void SdFirmwareUpdateActivity::onEnter() {
  Activity::onEnter();
  // Build-identity marker — confirms which firmware build owns the SD update flow.
  LOG_INF("FW", "SdFirmwareUpdateActivity build=%s %s recovery=%d", __DATE__, __TIME__, recoveryMode ? 1 : 0);
  if (!stagedPath.empty()) {
    firmwarePath = stagedPath;
    state = State::VALIDATING;
    stagedStartPending = true;
    requestUpdate();
    return;
  }
  state = State::PICKING;
  launchPicker();
}

void SdFirmwareUpdateActivity::launchPicker() {
  // Reuse the standard file browser, restricted to .bin files only.
  startActivityForResult(
      std::make_unique<FileBrowserActivity>(renderer, mappedInput, "/", FileBrowserActivity::Mode::PickFirmware),
      [this](const ActivityResult& result) { onPickerResult(result); });
}

void SdFirmwareUpdateActivity::onPickerResult(const ActivityResult& result) {
  if (result.isCancelled) {
    if (recoveryMode) {
      // Recovery mode: re-launch the picker so the user cannot escape into a half-initialised UI.
      launchPicker();
      return;
    }
    finish();
    return;
  }

  const auto* path = std::get_if<FilePathResult>(&result.data);
  if (!path) {
    LOG_ERR("FW", "Picker returned no path");
    finish();
    return;
  }
  firmwarePath = path->path;
  LOG_DBG("FW", "Selected: %s", firmwarePath.c_str());

  {
    RenderLock lock(*this);
    state = State::VALIDATING;
  }
  requestUpdateAndWait();

  if (!validateFirmware()) {
    RenderLock lock(*this);
    state = State::FAILED;
    requestUpdate();
    return;
  }

  promptConfirmation();
}

bool SdFirmwareUpdateActivity::validateFirmware() {
  HalFile file;
  if (!Storage.openFileForRead("FW", firmwarePath.c_str(), file) || !file) {
    errorMessage = tr(STR_FIRMWARE_FILE_OPEN_FAILED);
    return false;
  }
  firmwareSize = file.fileSize();
  file.close();

  // Resolve the next-update partition directly via the OTA API. Previously this
  // probed via Update.begin(firmwareSize)/Update.abort() to learn the partition
  // size, which had the side effect of erasing partition state and was wasted
  // work since we only need the size bound for validation here.
  const esp_partition_t* dest = esp_ota_get_next_update_partition(nullptr);
  if (!dest) {
    LOG_ERR("FW", "no next-update partition available");
    errorMessage = tr(STR_INVALID_FIRMWARE);
    return false;
  }
  const size_t partitionLimit = dest->size;
  if (firmwareSize > partitionLimit) {
    LOG_ERR("FW", "firmware (%u bytes) exceeds partition (%u bytes)", static_cast<unsigned>(firmwareSize),
            static_cast<unsigned>(partitionLimit));
    errorMessage = tr(STR_FIRMWARE_TOO_LARGE);
    return false;
  }

  // Run the same end-to-end integrity check (header / segment table / XOR checksum / SHA256
  // trailer) that the shared firmware-flasher applies right before raw-writing otadata. This
  // catches truncated or corrupted .bin files at confirmation time, before the user ever sees
  // the "Updating…" progress bar.
  const auto vr = firmware_flash::validateImageFile(firmwarePath.c_str(), partitionLimit);
  if (vr != firmware_flash::Result::OK) {
    LOG_ERR("FW", "image validation failed: %s", firmware_flash::resultName(vr));
    if (vr == firmware_flash::Result::TOO_LARGE) {
      errorMessage = tr(STR_FIRMWARE_TOO_LARGE);
    } else if (vr == firmware_flash::Result::TOO_SMALL) {
      errorMessage = tr(STR_FIRMWARE_TOO_SMALL);
    } else if (vr == firmware_flash::Result::BAD_CHIP || vr == firmware_flash::Result::WRONG_BOARD) {
      errorMessage = tr(STR_FIRMWARE_WRONG_DEVICE);
    } else {
      errorMessage = tr(STR_INVALID_FIRMWARE);
    }
    return false;
  }
  readImageInfo();
  return true;
}

// Version and build date come from the esp_app_desc_t that opens the first
// segment (image header 24 B + segment header 8 B = offset 32); the SHA-256 is
// the trailer the validation pass above just verified against the whole image.
void SdFirmwareUpdateActivity::readImageInfo() {
  constexpr size_t IMAGE_HEADER = 24;
  constexpr size_t SEGMENT_HEADER = 8;
  constexpr size_t APP_DESC_OFFSET = IMAGE_HEADER + SEGMENT_HEADER;
  constexpr size_t SHA_SIZE = 32;
  constexpr size_t HASH_APPENDED_AT = 23;
  constexpr uint32_t APP_DESC_MAGIC = 0xABCD5432;
  // esp_app_desc_t: magic(4) secure_version(4) reserv1(8) version[32]
  // project_name[32] time[16] date[16] ...; only these first 112 bytes are read.
  constexpr size_t VERSION_AT = 16;
  constexpr size_t TIME_AT = 80;
  constexpr size_t DATE_AT = 96;
  constexpr size_t APP_DESC_READ = 112;

  infoVersion[0] = infoBuilt[0] = infoSha[0] = '\0';
  HalFile file;
  if (!Storage.openFileForRead("FW", firmwarePath.c_str(), file) || !file) return;

  uint8_t header[IMAGE_HEADER];
  if (file.read(header, sizeof(header)) == static_cast<int>(sizeof(header)) && file.seekSet(APP_DESC_OFFSET)) {
    uint8_t desc[APP_DESC_READ];
    if (file.read(desc, sizeof(desc)) == static_cast<int>(sizeof(desc))) {
      uint32_t magic = 0;
      memcpy(&magic, desc, sizeof(magic));
      if (magic == APP_DESC_MAGIC) {
        char date[17];
        char time[17];
        memcpy(infoVersion, desc + VERSION_AT, 32);
        infoVersion[32] = '\0';
        memcpy(date, desc + DATE_AT, 16);
        date[16] = '\0';
        memcpy(time, desc + TIME_AT, 16);
        time[16] = '\0';
        snprintf(infoBuilt, sizeof(infoBuilt), "%s %s", date, time);
      }
    }
    if (header[HASH_APPENDED_AT] == 1 && firmwareSize > SHA_SIZE && file.seekSet(firmwareSize - SHA_SIZE)) {
      uint8_t sha[4];
      if (file.read(sha, sizeof(sha)) == static_cast<int>(sizeof(sha))) {
        snprintf(infoSha, sizeof(infoSha), "%02x%02x%02x%02x", sha[0], sha[1], sha[2], sha[3]);
      }
    }
  }
  LOG_INF("FW", "Image: version '%s' built '%s' sha256 %s...", infoVersion, infoBuilt, infoSha);
}

void SdFirmwareUpdateActivity::promptConfirmation() {
  {
    RenderLock lock(*this);
    state = State::CONFIRMING;
    holdStartMs = 0;
    holdShownSeconds = 0;
  }
  requestUpdate();
}

void SdFirmwareUpdateActivity::cancelConfirmation() {
  if (recoveryMode) {
    // Go back to the picker rather than exiting recovery.
    state = State::PICKING;
    launchPicker();
    return;
  }
  finish();
}

void SdFirmwareUpdateActivity::startUpdate() {
  {
    RenderLock lock(*this);
    state = State::UPDATING;
    writtenBytes = 0;
    lastRenderedPercent = 101;
  }
  requestUpdateAndWait();
  performUpdate();
}

void SdFirmwareUpdateActivity::performUpdate() {
  LOG_INF("FW", "SD update: %s (%u bytes)", firmwarePath.c_str(), static_cast<unsigned>(firmwareSize));

  auto progressCb = +[](size_t written, size_t total, void* ctx) {
    auto* self = static_cast<SdFirmwareUpdateActivity*>(ctx);
    self->writtenBytes = written;
    self->firmwareSize = total;
    // immediate=true: wake the render task directly. We're in a tight sync
    // loop so the main loop won't drain the requestedUpdate flag for us.
    self->requestUpdate(true);
  };

  // Re-validate at flash time (TOCTOU): SD is removable, so don't trust the
  // pre-confirmation pass. The alreadyValidated parameter on the API stays
  // for callers (e.g. an OTA staging path) where the same byte stream was
  // just hashed and there's no removable-media gap.
  const auto result = firmware_flash::flashFromSdPath(firmwarePath.c_str(), progressCb, this);
  if (result != firmware_flash::Result::OK) {
    LOG_ERR("FW", "flash failed: %s", firmware_flash::resultName(result));
    // BAD_CHIP / WRONG_BOARD here is the TOCTOU re-validation catching a
    // wrong-device image the pre-confirmation pass missed (e.g. the SD card
    // was swapped).
    errorMessage = result == firmware_flash::Result::BAD_CHIP || result == firmware_flash::Result::WRONG_BOARD
                       ? tr(STR_FIRMWARE_WRONG_DEVICE)
                       : tr(STR_FIRMWARE_WRITE_FAILED);
    RenderLock lock(*this);
    state = State::FAILED;
    requestUpdate();
    return;
  }

  LOG_INF("FW", "SD firmware update complete, restarting");
  if (!stagedPath.empty()) Storage.remove(firmwarePath.c_str());
  {
    RenderLock lock(*this);
    state = State::SUCCESS;
  }
  requestUpdateAndWait();
  delay(1500);
  ESP.restart();
}

void SdFirmwareUpdateActivity::loop() {
  if (stagedStartPending) {
    stagedStartPending = false;
    // Let "Validating" reach the panel before the blocking checksum pass.
    requestUpdateAndWait();
    if (!validateFirmware()) {
      RenderLock lock(*this);
      state = State::FAILED;
      requestUpdate();
      return;
    }
    promptConfirmation();
    return;
  }
  if (state == State::CONFIRMING) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      cancelConfirmation();
      return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) holdStartMs = millis();
    if (holdStartMs != 0 && mappedInput.isPressed(MappedInputManager::Button::Confirm)) {
      const unsigned long held = millis() - holdStartMs;
      if (held >= CONFIRM_HOLD_MS) {
        holdStartMs = 0;
        startUpdate();
        return;
      }
      const unsigned seconds = static_cast<unsigned>(held / 1000UL);
      if (seconds != holdShownSeconds) {
        holdShownSeconds = seconds;
        requestUpdate();
      }
    } else if (holdStartMs != 0) {
      holdStartMs = 0;  // released early
      holdShownSeconds = 0;
      requestUpdate();
    }
    return;
  }
  if (state == State::FAILED) {
    int x = 0;
    int y = 0;
    if (mappedInput.wasPressed(MappedInputManager::Button::Back) ||
        mappedInput.wasPressed(MappedInputManager::Button::Confirm) || mappedInput.wasScreenTapped(x, y)) {
      if (recoveryMode) {
        // Go back to picker so user can try a different .bin
        state = State::PICKING;
        launchPicker();
        return;
      }
      finish();
    }
  }
}

void SdFirmwareUpdateActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();

  const char* headerText = recoveryMode ? tr(STR_RECOVERY_MODE) : tr(STR_SD_FIRMWARE_UPDATE);
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, headerText);

  const auto lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const auto top = (pageHeight - lineHeight) / 2;

  if (state == State::VALIDATING) {
    renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_VALIDATING_FIRMWARE));
  } else if (state == State::CONFIRMING) {
    int y = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing * 2;
    const int side = metrics.contentSidePadding;
    const auto drawLine = [&](const char* text, const EpdFontFamily::Style style = EpdFontFamily::REGULAR) {
      const Rect bounds{side, y, pageWidth - side * 2, pageHeight - y};
      UITheme::drawCenteredWrappedText(renderer, bounds, UI_10_FONT_ID, text, 3, true, style,
                                       UITheme::TextVerticalAlignment::TOP);
      const int lines = static_cast<int>(renderer.wrappedText(UI_10_FONT_ID, text, bounds.width, 3, style).size());
      y += lineHeight * std::max(1, lines) + metrics.verticalSpacing;
    };
    drawLine(tr(STR_FIRMWARE_UPDATE_PROMPT), EpdFontFamily::BOLD);
    const auto slash = firmwarePath.find_last_of('/');
    drawLine(firmwarePath.c_str() + (slash == std::string::npos ? 0 : slash + 1));
    char line[96];
    if (infoVersion[0]) {
      snprintf(line, sizeof(line), tr(STR_FW_VERSION_LINE), infoVersion);
      drawLine(line);
    }
    if (infoBuilt[0]) {
      snprintf(line, sizeof(line), tr(STR_FW_BUILT_LINE), infoBuilt);
      drawLine(line);
    }
    if (infoSha[0]) {
      snprintf(line, sizeof(line), tr(STR_FW_SHA_LINE), infoSha);
      drawLine(line);
    }
    drawLine(tr(STR_FW_UNSIGNED_NOTE));
    y += metrics.verticalSpacing;
    if (holdStartMs != 0) {
      snprintf(line, sizeof(line), tr(STR_FW_HOLDING_LINE), holdShownSeconds + 1);
      drawLine(line, EpdFontFamily::BOLD);
    } else {
      drawLine(tr(STR_FW_HOLD_TO_INSTALL), EpdFontFamily::BOLD);
    }
    const auto labels = mappedInput.mapLabels(tr(STR_CANCEL), tr(STR_CONFIRM), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (state == State::UPDATING) {
    // Throttle redraws to once per percent.
    const unsigned int pct = firmwareSize > 0 ? static_cast<unsigned int>((writtenBytes * 100) / firmwareSize) : 0;
    if (pct == lastRenderedPercent) {
      return;
    }
    lastRenderedPercent = pct;

    renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_UPDATING), true, EpdFontFamily::BOLD);

    int y = top + lineHeight + metrics.verticalSpacing;
    GUI.drawProgressBar(
        renderer,
        Rect{metrics.contentSidePadding, y, pageWidth - metrics.contentSidePadding * 2, metrics.progressBarHeight},
        static_cast<int>(pct), 100);
    y += metrics.progressBarHeight + metrics.verticalSpacing;
    // Percent label is drawn by BaseTheme::drawProgressBar; this slot is left intentionally empty
    // so the do-not-power-off line below stays at the same Y as before.
    y += lineHeight + metrics.verticalSpacing;
    renderer.drawCenteredText(UI_10_FONT_ID, y, tr(STR_FIRMWARE_UPDATE_DO_NOT_POWER_OFF));
  } else if (state == State::SUCCESS) {
    renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_UPDATE_COMPLETE), true, EpdFontFamily::BOLD);
    const int hintY = top + lineHeight + metrics.verticalSpacing;
    const Rect hintBounds{metrics.contentSidePadding, hintY, pageWidth - metrics.contentSidePadding * 2,
                          pageHeight - hintY};
    UITheme::drawCenteredWrappedText(renderer, hintBounds, UI_10_FONT_ID, tr(STR_RESTARTING_HINT), 3, true,
                                     EpdFontFamily::REGULAR, UITheme::TextVerticalAlignment::TOP);
  } else if (state == State::FAILED) {
    renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_UPDATE_FAILED), true, EpdFontFamily::BOLD);
    if (!errorMessage.empty()) {
      renderer.drawCenteredText(UI_10_FONT_ID, top + lineHeight + metrics.verticalSpacing, errorMessage.c_str());
    }
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else {
    // PICKING / CONFIRMING: a sub-activity is on top, nothing to draw.
    if (recoveryMode) {
      renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_RECOVERY_MODE_HINT));
    }
  }

  renderer.displayBuffer();
}
