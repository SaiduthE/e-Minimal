#include "FirmwareInstallServer.h"

#include <ArduinoJson.h>
#include <BoardConfig.h>
#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>
#include <esp_ota_ops.h>

#include <algorithm>
#include <cstring>

#include "util/TaskWatchdog.h"

namespace {
constexpr const char* STAGING_DIR = "/.e-minimal/ota";
constexpr const char* TEMP_PATH = "/.e-minimal/ota/firmware.part";
constexpr size_t MIN_IMAGE_SIZE = 64 * 1024;
constexpr uint8_t IMAGE_MAGIC = 0xE9;  // first byte of every ESP32 app image
constexpr const char* PUSH_HEADER = "X-Eminimal-Update";
constexpr const char* DISCOVERY_QUERY = "eminimal?";

void sendJsonError(WebServer& server, const int code, const char* message) {
  JsonDocument doc;
  doc["error"] = message;
  String body;
  serializeJson(doc, body);
  server.send(code, "application/json", body);
}
}  // namespace

bool FirmwareInstallServer::begin(const bool announce) {
  server = makeUniqueNoThrow<WebServer>(80);
  if (!server) {
    LOG_ERR("FWI", "No memory for the install server");
    return false;
  }

  server->on("/api/status", HTTP_GET, [this] { handleStatus(); });
  server->on(
      "/api/firmware", HTTP_POST, [this] { handleUploadDone(); }, [this] { handleUploadData(); });
  server->onNotFound([this] { sendJsonError(*server, 404, "Not found"); });
  const char* headers[] = {PUSH_HEADER};
  server->collectHeaders(headers, 1);
  server->begin();

  udpActive = announce && udp.begin(DISCOVERY_PORT);
  lastActivity = millis();
  LOG_INF("FWI", "Install server up, discovery %s", udpActive ? "on" : "off");
  return true;
}

void FirmwareInstallServer::stop() {
  if (upload.file.isOpen()) upload.file.close();
  if (udpActive) {
    udp.stop();
    udpActive = false;
  }
  if (server) {
    server->stop();
    server.reset();
  }
}

void FirmwareInstallServer::handleClient() {
  if (!server) return;
  server->handleClient();
  if (udpActive) answerDiscovery();
}

bool FirmwareInstallServer::takeStaged(size_t& size) {
  if (stagedSize == 0) return false;
  size = stagedSize;
  stagedSize = 0;
  return true;
}

void FirmwareInstallServer::answerDiscovery() {
  if (udp.parsePacket() <= 0) return;
  char query[16];
  const int len = udp.read(query, sizeof(query) - 1);
  if (len <= 0) return;
  query[len] = '\0';
  if (strcmp(query, DISCOVERY_QUERY) != 0) return;

  char reply[96];
  const int n = snprintf(reply, sizeof(reply), "{\"device\":\"%s\",\"version\":\"%s\",\"ip\":\"%s\"}",
                         BoardConfig::ACTIVE.name, CROSSPOINT_VERSION, WiFi.localIP().toString().c_str());
  if (n <= 0 || n >= static_cast<int>(sizeof(reply))) return;
  udp.beginPacket(udp.remoteIP(), udp.remotePort());
  udp.write(reinterpret_cast<const uint8_t*>(reply), static_cast<size_t>(n));
  udp.endPacket();
}

void FirmwareInstallServer::handleStatus() {
  JsonDocument doc;
  doc["device"] = BoardConfig::ACTIVE.name;
  doc["version"] = CROSSPOINT_VERSION;
  doc["ip"] = WiFi.localIP().toString();
  doc["ready"] = stagedSize == 0 && !receiving;
  String body;
  serializeJson(doc, body);
  server->send(200, "application/json", body);
}

void FirmwareInstallServer::failUpload(const int code, const char* message) {
  upload.httpCode = code;
  upload.error = message;
}

void FirmwareInstallServer::handleUploadData() {
  HTTPUpload& up = server->upload();
  Upload& fw = upload;
  lastActivity = millis();

  switch (up.status) {
    case UPLOAD_FILE_START: {
      resetTaskWatchdogIfSubscribed();
      fw.file = HalFile();
      fw.size = 0;
      fw.bufferPos = 0;
      fw.started = true;
      fw.ok = false;
      fw.error = nullptr;
      fw.httpCode = 400;
      receiving = true;

      const esp_partition_t* slot = esp_ota_get_next_update_partition(nullptr);
      if (!server->hasHeader(PUSH_HEADER)) {
        failUpload(403, "Use tools/ota_push.py to send firmware");
      } else if (up.name != "firmware") {
        failUpload(400, "Expected multipart field \"firmware\"");
      } else if (stagedSize != 0) {
        failUpload(409, "A firmware image is already waiting for confirmation");
      } else if (!slot) {
        failUpload(500, "No OTA slot available");
      } else if (!Storage.ensureDirectoryExists(STAGING_DIR)) {
        failUpload(500, "Could not create the staging folder on the SD card");
      } else {
        fw.limit = slot->size;
        if (Storage.exists(TEMP_PATH)) Storage.remove(TEMP_PATH);
        if (!Storage.openFileForWrite("FWI", TEMP_PATH, fw.file)) {
          failUpload(500, "Could not create the staging file on the SD card");
        }
      }
      if (fw.error) {
        LOG_ERR("FWI", "Rejected: %s", fw.error);
      } else {
        LOG_INF("FWI", "Receiving firmware, slot limit %u bytes", static_cast<unsigned>(fw.limit));
      }
      break;
    }

    case UPLOAD_FILE_WRITE: {
      if (fw.error || !fw.file) break;
      resetTaskWatchdogIfSubscribed();
      if (fw.size == 0 && up.currentSize > 0 && up.buf[0] != IMAGE_MAGIC) {
        failUpload(400, "Not an ESP32 firmware image");
        break;
      }
      if (fw.size + up.currentSize > fw.limit) {
        failUpload(413, "Firmware is larger than the OTA slot");
        break;
      }
      const uint8_t* src = up.buf;
      size_t remaining = up.currentSize;
      while (remaining > 0) {
        const size_t chunk = std::min(remaining, BUFFER_SIZE - fw.bufferPos);
        memcpy(fw.buffer + fw.bufferPos, src, chunk);
        fw.bufferPos += chunk;
        src += chunk;
        remaining -= chunk;
        if (fw.bufferPos == BUFFER_SIZE) {
          if (fw.file.write(fw.buffer, fw.bufferPos) != fw.bufferPos) {
            failUpload(507, "Could not write to the SD card (full?)");
            break;
          }
          fw.bufferPos = 0;
          resetTaskWatchdogIfSubscribed();
        }
      }
      fw.size += up.currentSize;
      break;
    }

    case UPLOAD_FILE_END: {
      receiving = false;
      if (!fw.error && fw.file && fw.bufferPos > 0) {
        if (fw.file.write(fw.buffer, fw.bufferPos) != fw.bufferPos) {
          failUpload(507, "Could not write to the SD card (full?)");
        }
      }
      fw.bufferPos = 0;
      if (fw.file.isOpen()) fw.file.close();
      if (!fw.error && fw.size < MIN_IMAGE_SIZE) failUpload(400, "Firmware file is too small");
      if (!fw.error) {
        if (Storage.exists(STAGED_PATH)) Storage.remove(STAGED_PATH);
        if (!Storage.rename(TEMP_PATH, STAGED_PATH)) failUpload(500, "Could not finalise the staged file");
      }
      if (fw.error) {
        LOG_ERR("FWI", "Upload failed: %s", fw.error);
        if (Storage.exists(TEMP_PATH)) Storage.remove(TEMP_PATH);
      } else {
        fw.ok = true;
        LOG_INF("FWI", "Staged %u bytes at %s", static_cast<unsigned>(fw.size), STAGED_PATH);
      }
      break;
    }

    case UPLOAD_FILE_ABORTED: {
      receiving = false;
      fw.bufferPos = 0;
      if (fw.file.isOpen()) fw.file.close();
      if (Storage.exists(TEMP_PATH)) Storage.remove(TEMP_PATH);
      fw.ok = false;
      failUpload(400, "Upload aborted");
      LOG_ERR("FWI", "Upload aborted");
      break;
    }
  }
}

void FirmwareInstallServer::handleUploadDone() {
  lastActivity = millis();
  Upload& fw = upload;
  if (fw.started && fw.ok) {
    fw.started = false;
    fw.ok = false;
    char json[48];
    snprintf(json, sizeof(json), "{\"ok\":true,\"size\":%u}", static_cast<unsigned>(fw.size));
    server->send(200, "application/json", json);
    stagedSize = fw.size;  // the activity hands over to the install screen once this response is out
    return;
  }
  const int code = fw.started ? fw.httpCode : (server->hasHeader(PUSH_HEADER) ? 400 : 403);
  sendJsonError(*server, code,
                fw.started && fw.error ? fw.error : "Expected a multipart upload with field \"firmware\"");
  fw.started = false;
}
