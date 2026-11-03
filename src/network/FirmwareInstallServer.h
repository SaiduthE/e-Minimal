#pragma once

#include <HalStorage.h>
#include <NetworkUdp.h>
#include <WebServer.h>

#include <cstdint>
#include <memory>

// The whole HTTP surface of "Install Firmware over Wi-Fi": GET /api/status and POST /api/firmware (multipart field
// "firmware"), plus a UDP reply so tools/ota_push.py finds the reader without an address. No pages, no file access.
// The pushed image only waits on the SD card; the person confirms on the reader before anything is flashed. A
// browser cannot forge the push: the POST must carry the X-Eminimal-Update header, which a cross-site form cannot
// add without a preflight this server never answers.
class FirmwareInstallServer {
 public:
  static constexpr const char* STAGED_PATH = "/.e-minimal/ota/firmware.bin";
  static constexpr uint16_t DISCOVERY_PORT = 8135;

  // announce: answer the UDP discovery broadcast (the mDNS name is the activity's).
  bool begin(bool announce);
  void stop();
  void handleClient();
  bool isRunning() const { return server != nullptr; }

  // True while an image is being received.
  bool isReceiving() const { return receiving; }
  // True once, right after an image finished uploading; `size` is its byte count.
  bool takeStaged(size_t& size);
  // millis() of the last HTTP request or upload chunk.
  uint32_t lastActivityMs() const { return lastActivity; }

 private:
  static constexpr size_t BUFFER_SIZE = 4096;  // batches the 1.4 KB upload chunks into SD-friendly writes

  std::unique_ptr<WebServer> server;
  NetworkUDP udp;
  bool udpActive = false;
  uint32_t lastActivity = 0;
  size_t stagedSize = 0;  // 0 = nothing pending
  bool receiving = false;

  struct Upload {
    HalFile file;
    size_t size = 0;
    size_t limit = 0;  // OTA slot size
    size_t bufferPos = 0;
    bool started = false;
    bool ok = false;
    int httpCode = 400;
    const char* error = nullptr;
    uint8_t buffer[BUFFER_SIZE];
  } upload;

  void handleStatus();
  void handleUploadData();
  void handleUploadDone();
  void answerDiscovery();
  void failUpload(int code, const char* message);
};
