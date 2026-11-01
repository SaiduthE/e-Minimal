#pragma once

#include <string>

#include "activities/Activity.h"

/**
 * SD-card based firmware update activity.
 *
 * Flow:
 *  1) onEnter -> push FileBrowserActivity in PickFirmware mode (only .bin files visible).
 *  2) On result: validate the .bin (header magic, size fits OTA partition).
 *  3) Show the image's version, build date and SHA-256 prefix, and wait for a 3 s hold on Confirm.
 *  4) On confirm: stream the file into the OTA partition via the Arduino Update API,
 *     drawing a progress bar; on success ESP.restart().
 *
 * Used both from Settings -> System -> "SD Card Firmware Update", and as the only
 * activity launched in boot recovery mode (left side button + power on X3).
 */
class SdFirmwareUpdateActivity : public Activity {
 public:
  enum class State {
    PICKING,
    VALIDATING,
    CONFIRMING,
    UPDATING,
    SUCCESS,
    FAILED,
  };

  // stagedPath: an image already on the SD card (Wi-Fi push). It skips the picker, validates, asks for the same
  // confirmation, flashes, deletes the file and restarts; declining finishes without flashing.
  explicit SdFirmwareUpdateActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, bool recoveryMode = false,
                                    std::string stagedPath = {})
      : Activity("SdFirmwareUpdate", renderer, mappedInput),
        recoveryMode(recoveryMode),
        stagedPath(std::move(stagedPath)) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return state == State::UPDATING || state == State::VALIDATING; }
  bool skipLoopDelay() override { return state == State::UPDATING; }
  static constexpr unsigned long CONFIRM_HOLD_MS = 3000;

 private:
  State state = State::PICKING;
  bool recoveryMode = false;
  std::string stagedPath;
  bool stagedStartPending = false;

  std::string firmwarePath;
  size_t firmwareSize = 0;
  size_t writtenBytes = 0;
  unsigned int lastRenderedPercent = 101;
  std::string errorMessage;

  // Shown on the confirm screen, read from the validated image.
  char infoVersion[33] = "";
  char infoBuilt[40] = "";
  char infoSha[9] = "";
  unsigned long holdStartMs = 0;  // 0 = Confirm not held
  unsigned holdShownSeconds = 0;

  void readImageInfo();
  void startUpdate();
  void cancelConfirmation();

  void launchPicker();
  void onPickerResult(const ActivityResult& result);
  bool validateFirmware();
  void promptConfirmation();
  void performUpdate();
};
