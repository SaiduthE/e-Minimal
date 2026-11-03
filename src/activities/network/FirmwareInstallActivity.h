#pragma once

#include <memory>
#include <string>

#include "activities/Activity.h"
#include "network/FirmwareInstallServer.h"

/**
 * Install Firmware over Wi-Fi. Joins a saved network, serves FirmwareInstallServer and shows the address while
 * tools/ota_push.py sends a build. A pushed image only waits on the SD card: SdFirmwareUpdateActivity shows its
 * version, build date and hash and needs a 3 s hold on Confirm before it flashes. Leaves by itself after
 * IDLE_STOP_MS with no request.
 */
class FirmwareInstallActivity final : public Activity {
  enum class State : uint8_t { JOINING, WAITING, INSTALLING };

  static constexpr unsigned long IDLE_STOP_MS = 5UL * 60UL * 1000UL;

  State state = State::JOINING;
  std::unique_ptr<FirmwareInstallServer> server;
  std::string ssid;
  std::string ip;
  bool wifiStarted = false;
  bool announced = false;   // mDNS name up
  bool receiving = false;   // last rendered receiving state

  void onWifiResult(bool connected);
  void startServer();
  void installStaged();

 public:
  explicit FirmwareInstallActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("FirmwareInstall", renderer, mappedInput) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool skipLoopDelay() override { return state == State::WAITING; }
  bool preventAutoSleep() override { return true; }
};
