#include "CrossPointWebServer.h"

#include <ArduinoJson.h>
#include <BoardConfig.h>
#include <FsHelpers.h>
#include <HalGPIO.h>
#include <HalStorage.h>
#include <LibraryBuilder.h>
#include <Logging.h>
#include <WiFi.h>
#include <esp_efuse.h>
#include <esp_efuse_table.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "CrossPointSettings.h"
#include "DashboardConfigStore.h"
#include "FontInstaller.h"
#include "OpdsServerStore.h"
#include "PhonePage.h"
#include "SdCardFontSystem.h"
#include "SettingsList.h"
#include "TodoStore.h"
#include "WebDAVHandler.h"
#include "WifiCredentialStore.h"
#include "html/ConvertPageHtml.generated.h"
#include "html/DashboardsPageHtml.generated.h"
#include "html/FilesPageHtml.generated.h"
#include "html/FontsPageHtml.generated.h"
#include "html/HomePageHtml.generated.h"
#include "html/PicturesPageHtml.generated.h"
#include "html/SettingsPageHtml.generated.h"
#include "html/css/appCss.generated.h"
#include "html/js/appJs.generated.h"
#include "html/js/bindery_appJs.generated.h"
#include "html/js/bindery_coreJs.generated.h"
#include "html/js/jszip_minJs.generated.h"
#include "html/js/pdf_minJs.generated.h"
#include "html/js/pdf_worker_minJs.generated.h"
#include "util/BookCacheUtils.h"
#include "util/TaskWatchdog.h"

namespace {
// Folders/files to hide from the web interface file browser
// Note: Items starting with "." are automatically hidden
constexpr const char* HIDDEN_ITEMS[] = {"System Volume Information", "XTCache"};

// Formats the library index tracks (LibraryIndex isBookName): an upload of any
// of these must mark the index dirty so the next Library entry rebuilds it.
bool isLibraryBookFile(const String& filename) {
  return FsHelpers::checkFileExtension(filename, ".epub") || FsHelpers::checkFileExtension(filename, ".txt") ||
         FsHelpers::checkFileExtension(filename, ".md") || FsHelpers::checkFileExtension(filename, ".xtc") ||
         FsHelpers::checkFileExtension(filename, ".xtch");
}
constexpr uint16_t UDP_PORTS[] = {54982, 48123, 39001, 44044, 59678};
constexpr uint16_t LOCAL_UDP_PORT = 8134;

// Static pointer for WebSocket callback (WebSocketsServer requires C-style callback)
CrossPointWebServer* wsInstance = nullptr;

// WebSocket upload state
HalFile wsUploadFile;
String wsUploadFileName;
String wsUploadPath;
size_t wsUploadSize = 0;
size_t wsUploadReceived = 0;
unsigned long wsUploadStartTime = 0;
bool wsUploadInProgress = false;
uint8_t wsUploadClientNum = 255;  // 255 = no active upload client
size_t wsLastProgressSent = 0;
String wsLastCompleteName;
size_t wsLastCompleteSize = 0;
unsigned long wsLastCompleteAt = 0;

String normalizeWebPath(const String& inputPath) {
  if (inputPath.isEmpty() || inputPath == "/") {
    return "/";
  }
  std::string normalized = FsHelpers::normalisePath(inputPath.c_str());
  String result = normalized.c_str();
  if (result.isEmpty()) {
    return "/";
  }
  if (!result.startsWith("/")) {
    result = "/" + result;
  }
  if (result.length() > 1 && result.endsWith("/")) {
    result = result.substring(0, result.length() - 1);
  }
  return result;
}

bool isProtectedItemName(const String& name) {
  if (name.startsWith(".")) {
    return true;
  }
  for (const auto* item : HIDDEN_ITEMS) {
    if (name.equals(item)) {
      return true;
    }
  }
  return false;
}

// Answers {"error":message} or, when a JSON key was rejected, {"error":message,"field":"weather.lat"}.
void sendJsonError(WebServer& server, const int code, const char* message, const char* field = nullptr) {
  JsonDocument doc;
  doc["error"] = message;
  if (field) doc["field"] = field;
  String json;
  serializeJson(doc, json);
  server.send(code, "application/json", json);
}

// Four decimals (about 11 m) is finer than any forecast grid and keeps float
// noise out of the JSON ("52.52", not "52.520000457763672").
double roundCoordinate(const float degrees) { return std::round(static_cast<double>(degrees) * 10000.0) / 10000.0; }

// A JSON path built at run time for an error ("saveLayout.tiles[2]"). The web server answers one request at a time,
// so one buffer serves.
char fieldPath[40];

const char* prefixedField(const char* prefix, const char* field) {
  snprintf(fieldPath, sizeof(fieldPath), "%s.%s", prefix, field);
  return fieldPath;
}

constexpr const char* TILE_FIELDS[] = {"tiles[0]", "tiles[1]", "tiles[2]", "tiles[3]",
                                       "tiles[4]", "tiles[5]", "tiles[6]", "tiles[7]"};
static_assert(sizeof(TILE_FIELDS) / sizeof(TILE_FIELDS[0]) == CustomLayout::MAX_TILES, "one field name per tile");

// Well-formed UTF-8 (lead and continuation bytes line up) without control characters: text the device fonts can
// step through and draw on one line.
bool isCleanText(const char* text, const size_t len) {
  size_t i = 0;
  while (i < len) {
    const auto c = static_cast<uint8_t>(text[i]);
    if (c < 0x20 || c == 0x7F) return false;
    size_t extra = 0;
    if (c >= 0xC2 && c <= 0xDF) {
      extra = 1;
    } else if ((c & 0xF0) == 0xE0) {
      extra = 2;
    } else if (c >= 0xF0 && c <= 0xF4) {
      extra = 3;
    } else if (c >= 0x80) {
      return false;
    }
    if (len - i <= extra) return false;
    for (size_t k = 1; k <= extra; k++) {
      if ((static_cast<uint8_t>(text[i + k]) & 0xC0) != 0x80) return false;
    }
    i += extra + 1;
  }
  return true;
}

// The fields a POST /api/dashboards may change. The store's setters apply as
// they validate, so a rejected request restores this copy to leave memory
// matching the card.
struct DashboardSnapshot {
  std::vector<CustomLayout> layouts;
  uint8_t activeLayoutId;
  std::string weatherPlace;
  float weatherLatitude;
  float weatherLongitude;
  bool weatherHasLocation;
  bool weatherFahrenheit;
  std::string carouselFolder;
  uint16_t carouselIntervalMinutes;

  explicit DashboardSnapshot(const DashboardConfigStore& config)
      : layouts(config.layouts),
        activeLayoutId(config.activeLayoutId),
        weatherPlace(config.weatherPlace),
        weatherLatitude(config.weatherLatitude),
        weatherLongitude(config.weatherLongitude),
        weatherHasLocation(config.weatherHasLocation),
        weatherFahrenheit(config.weatherFahrenheit),
        carouselFolder(config.carouselFolder),
        carouselIntervalMinutes(config.carouselIntervalMinutes) {}

  void restore(DashboardConfigStore& config) const {
    config.layouts = layouts;
    config.activeLayoutId = activeLayoutId;
    config.weatherPlace = weatherPlace;
    config.weatherLatitude = weatherLatitude;
    config.weatherLongitude = weatherLongitude;
    config.weatherHasLocation = weatherHasLocation;
    config.weatherFahrenheit = weatherFahrenheit;
    config.carouselFolder = carouselFolder;
    config.carouselIntervalMinutes = carouselIntervalMinutes;
  }
};

// CustomLayout::fromJson() only reports that something was unreadable; this
// names it in validate()'s terms ("name", "shape", "tiles", "tiles[i]").
const char* unreadableLayoutField(JsonObjectConst in) {
  if (!in["name"].is<const char*>()) return "name";
  const char* shape = in["shape"] | "landscape";
  if (strcmp(shape, "landscape") != 0 && strcmp(shape, "portrait") != 0) return "shape";
  if (!in["tiles"].is<JsonArrayConst>()) return "tiles";
  JsonArrayConst tiles = in["tiles"].as<JsonArrayConst>();
  if (tiles.size() > CustomLayout::MAX_TILES) return "tiles";
  size_t index = 0;
  for (JsonVariantConst tile : tiles) {
    WidgetId widget = WidgetId::NONE;
    bool readable = tile.is<JsonObjectConst>() && DashboardConfigStore::widgetFromKey(tile["widget"] | "", widget);
    for (const char* key : {"x", "y", "w", "h"}) {
      readable = readable && tile[key].is<int>() && tile[key].as<int>() >= 0 && tile[key].as<int>() <= 255;
    }
    if (!readable) return TILE_FIELDS[index];
    index++;
  }
  return "tiles";
}

// What a POST /api/dashboards did besides changing the store, for its answer.
struct DashboardOutcome {
  uint8_t savedId = 0;        // the id a saveLayout got
  bool layoutsFull = false;   // saveLayout found no room
  bool restoreAsked = false;  // restoreDefaults was true
  uint8_t restored = 0;       // starting layouts it added back
};

// saveLayout: a new (id 0) or edited layout, saved and put in use.
const char* applySaveLayout(DashboardConfigStore& config, JsonVariantConst value, DashboardOutcome& outcome) {
  if (!value.is<JsonObjectConst>()) return "saveLayout";
  JsonObjectConst in = value.as<JsonObjectConst>();
  const int id = in["id"].isNull() ? 0 : (in["id"].is<int>() ? in["id"].as<int>() : -1);
  if (id < 0 || id > 255 || (id != 0 && !config.findLayout(static_cast<uint8_t>(id)))) return "saveLayout.id";

  CustomLayout layout;
  const char* field = nullptr;
  if (!layout.fromJson(in)) {
    field = unreadableLayoutField(in);
  } else if (!isCleanText(layout.name.c_str(), layout.name.size())) {
    field = "name";
  } else {
    field = layout.validate();
  }
  if (field) return prefixedField("saveLayout", field);

  layout.id = static_cast<uint8_t>(id);
  if (!config.saveLayout(layout)) {
    outcome.layoutsFull = true;
    return "saveLayout";
  }
  config.activateLayout(layout.id);
  outcome.savedId = layout.id;
  return nullptr;
}

// Applies the fields present in a POST /api/dashboards body. Returns the JSON
// path of the first rejected field, or nullptr when all of them were accepted.
const char* applyDashboardChanges(DashboardConfigStore& config, JsonVariantConst body, DashboardOutcome& outcome) {
  JsonVariantConst activeValue = body["layout"]["active"];
  if (!activeValue.isNull()) {
    const int id = activeValue.is<int>() ? activeValue.as<int>() : -1;
    // 0, as a GET reports "none", passes only while no layout is in use.
    const bool keepsNone = id == 0 && !config.activeLayout();
    if (!keepsNone && (id <= 0 || id > 255 || !config.activateLayout(static_cast<uint8_t>(id)))) {
      return "layout.active";
    }
  }

  JsonVariantConst saveValue = body["saveLayout"];
  if (!saveValue.isNull()) {
    const char* field = applySaveLayout(config, saveValue, outcome);
    if (field) return field;
  }

  JsonVariantConst deleteValue = body["deleteLayout"];
  if (!deleteValue.isNull()) {
    const int id = deleteValue.is<int>() ? deleteValue.as<int>() : -1;
    if (id <= 0 || id > 255 || !config.deleteLayout(static_cast<uint8_t>(id))) return "deleteLayout";
  }

  JsonVariantConst restoreValue = body["restoreDefaults"];
  if (!restoreValue.isNull()) {
    if (!restoreValue.is<bool>()) return "restoreDefaults";
    if (restoreValue.as<bool>()) {
      const bool portrait = SETTINGS.dashboardOrientation == CrossPointSettings::DASHBOARD_PORTRAIT;
      outcome.restoreAsked = true;
      outcome.restored = config.restoreDefaultLayouts(portrait);
      // Layouts back after all were deleted: the first goes on the dashboard.
      if (!config.activeLayout() && !config.layouts.empty()) config.activateLayout(config.layouts.front().id);
    }
  }

  JsonVariantConst weather = body["weather"];
  if (weather["clearLocation"] | false) {
    config.clearWeatherLocation();
  } else if (weather["hasLocation"] | true) {  // false: the 0,0 placeholders of an echoed GET
    JsonVariantConst lat = weather["lat"];
    JsonVariantConst lon = weather["lon"];
    JsonVariantConst place = weather["place"];
    if (!lat.isNull() || !lon.isNull()) {
      if (!lat.is<float>()) return "weather.lat";
      if (!lon.is<float>()) return "weather.lon";
      if (!place.isNull() && !place.is<const char*>()) return "weather.place";
      const float latitude = lat.as<float>();
      if (!config.setWeatherLocation(place | "", latitude, lon.as<float>())) {
        // The setter checks both; name the one that is out of range.
        const bool latitudeOk = std::isfinite(latitude) && latitude >= -90.0f && latitude <= 90.0f;
        return latitudeOk ? "weather.lon" : "weather.lat";
      }
    }
  }

  JsonVariantConst units = weather["units"];
  if (!units.isNull()) {
    const char* value = units | "";
    if (strcmp(value, "c") == 0) {
      config.weatherFahrenheit = false;
    } else if (strcmp(value, "f") == 0) {
      config.weatherFahrenheit = true;
    } else {
      return "weather.units";
    }
  }

  JsonVariantConst carousel = body["carousel"];
  JsonVariantConst folder = carousel["folder"];
  if (!folder.isNull() && (!folder.is<const char*>() || !config.setCarouselFolder(folder.as<const char*>()))) {
    return "carousel.folder";
  }
  JsonVariantConst interval = carousel["intervalMinutes"];
  if (!interval.isNull() && (!interval.is<int>() || !config.setCarouselIntervalMinutes(interval.as<int>()))) {
    return "carousel.intervalMinutes";
  }
  return nullptr;
}

// Answers GET /api/todo's shape from the loaded TodoStore.
void sendTodoList(WebServer& server) {
  JsonDocument doc;
  JsonArray items = doc["items"].to<JsonArray>();
  for (const auto& item : TODO_STORE.getItems()) {
    JsonObject entry = items.add<JsonObject>();
    entry["text"] = item.text.c_str();
    entry["done"] = item.done;
  }
  doc["max"] = static_cast<uint32_t>(TodoStore::MAX_ITEMS);
  doc["maxText"] = static_cast<uint32_t>(TodoStore::MAX_TEXT_LENGTH);
  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

}  // namespace

// File listing page template - now using generated headers:
// - HomePageHtml (from html/HomePage.html)
// - FilesPageHeaderHtml (from html/FilesPageHeader.html)
// - FilesPageFooterHtml (from html/FilesPageFooter.html)
CrossPointWebServer::CrossPointWebServer() {}

CrossPointWebServer::~CrossPointWebServer() { stop(); }

void CrossPointWebServer::begin() {
  if (running) {
    LOG_DBG("WEB", "Web server already running");
    return;
  }

  // Check if we have a valid network connection (either STA connected or AP mode)
  const wifi_mode_t wifiMode = WiFi.getMode();
  const bool isStaConnected = (wifiMode & WIFI_MODE_STA) && (WiFi.status() == WL_CONNECTED);
  const bool isInApMode = (wifiMode & WIFI_MODE_AP) && (WiFi.softAPgetStationNum() >= 0);  // AP is running

  if (!isStaConnected && !isInApMode) {
    LOG_DBG("WEB", "Cannot start webserver - no valid network (mode=%d, status=%d)", wifiMode, WiFi.status());
    return;
  }

  // Store AP mode flag for later use (e.g., in handleStatus)
  apMode = isInApMode;

  LOG_DBG("WEB", "[MEM] Free heap before begin: %d bytes", ESP.getFreeHeap());
  LOG_DBG("WEB", "Network mode: %s", apMode ? "AP" : "STA");

  LOG_DBG("WEB", "Creating web server on port %d...", port);
  server.reset(new WebServer(port));

  // Disable WiFi sleep to improve responsiveness and prevent 'unreachable' errors.
  // This is critical for reliable web server operation on ESP32.
  WiFi.setSleep(false);
  // Default varies by ESP32 core version. The activity's loss-recovery loop
  // relies on driver retries during transient disconnects.
  WiFi.setAutoReconnect(true);

  // Note: WebServer class doesn't have setNoDelay() in the standard ESP32 library.
  // We rely on disabling WiFi sleep for responsiveness.

  LOG_DBG("WEB", "[MEM] Free heap after WebServer allocation: %d bytes", ESP.getFreeHeap());

  if (!server) {
    LOG_ERR("WEB", "Failed to create WebServer!");
    return;
  }

  // Add Access-Control-Allow-* headers to every response so web-based clients
  // and PWAs on other origins can use the HTTP API. Preflight OPTIONS requests
  // are answered in handleNotFound().
  server->enableCORS(true);

  // Setup routes
  LOG_DBG("WEB", "Setting up routes...");
  server->on("/", HTTP_GET, [this] { handleRoot(); });
  server->on("/files", HTTP_GET, [this] { handleFileList(); });
  server->on("/js/jszip.min.js", HTTP_GET, [this] { handleJszip(); });
  // The look every page shares, served once and cached by ETag.
  server->on("/css/app.css", HTTP_GET, [this] { handleAppCss(); });
  server->on("/js/app.js", HTTP_GET, [this] { handleAppJs(); });

  server->on("/api/status", HTTP_GET, [this] { handleStatus(); });
  server->on("/api/files", HTTP_GET, [this] { handleFileListData(); });
  server->on("/download", HTTP_GET, [this] { handleDownload(); });

  // Upload endpoint with special handling for multipart form data
  server->on("/upload", HTTP_POST, [this] { handleUploadPost(upload); }, [this] { handleUpload(upload); });

  // Create folder endpoint
  server->on("/mkdir", HTTP_POST, [this] { handleCreateFolder(); });

  // Rename file endpoint
  server->on("/rename", HTTP_POST, [this] { handleRename(); });

  // Move file endpoint
  server->on("/move", HTTP_POST, [this] { handleMove(); });

  // Delete file/folder endpoint
  server->on("/delete", HTTP_POST, [this] { handleDelete(); });

  // Settings endpoints
  server->on("/settings", HTTP_GET, [this] { handleSettingsPage(); });
  server->on("/api/settings", HTTP_GET, [this] { handleGetSettings(); });
  server->on("/api/settings", HTTP_POST, [this] { handlePostSettings(); });

  // Convert: manga, comics and books made ready on the phone, streamed to
  // the card over the WebSocket upload. pdf.js (~380 KB gzipped) is only
  // fetched when a PDF is added.
  server->on("/convert", HTTP_GET, [this] {
    PhonePage::sendStatic(*server, ConvertPageHtml, sizeof(ConvertPageHtml), ConvertPageHtmlETag, "text/html");
  });
  server->on("/js/bindery-core.js", HTTP_GET, [this] {
    PhonePage::sendStatic(*server, bindery_coreJs, bindery_coreJsCompressedSize, bindery_coreJsETag,
                          "application/javascript");
  });
  server->on("/js/bindery-app.js", HTTP_GET, [this] {
    PhonePage::sendStatic(*server, bindery_appJs, bindery_appJsCompressedSize, bindery_appJsETag,
                          "application/javascript");
  });
  server->on("/js/pdf.min.js", HTTP_GET, [this] {
    PhonePage::sendStatic(*server, pdf_minJs, pdf_minJsCompressedSize, pdf_minJsETag, "application/javascript");
  });
  server->on("/js/pdf.worker.min.js", HTTP_GET, [this] {
    PhonePage::sendStatic(*server, pdf_worker_minJs, pdf_worker_minJsCompressedSize, pdf_worker_minJsETag,
                          "application/javascript");
  });

  // Font management endpoints
  server->on("/fonts", HTTP_GET, [this] { handleFontsPage(); });
  server->on("/api/fonts", HTTP_GET, [this] { handleFontList(); });
  server->on("/api/fonts/upload", HTTP_POST, [this] { handleFontUpload(); }, [this] { handleFontUploadData(); });
  server->on("/api/fonts/delete", HTTP_POST, [this] { handleFontDelete(); });

  // OPDS server endpoints
  server->on("/api/opds", HTTP_GET, [this] { handleGetOpdsServers(); });
  server->on("/api/opds", HTTP_POST, [this] { handlePostOpdsServer(); });
  server->on("/api/opds/delete", HTTP_POST, [this] { handleDeleteOpdsServer(); });

  // Wi-Fi credential endpoints
  server->on("/api/wifi", HTTP_GET, [this] { handleGetWifiNetworks(); });
  server->on("/api/wifi", HTTP_POST, [this] { handlePostWifiNetwork(); });
  server->on("/api/wifi/delete", HTTP_POST, [this] { handleDeleteWifiNetwork(); });

  // Dashboard endpoints: layouts, widget settings, the to-do list, pictures for the Image tile
  server->on("/dashboards", HTTP_GET, [this] { handleDashboardsPage(); });
  server->on("/pictures", HTTP_GET, [this] { handlePicturesPage(); });
  server->on("/api/dashboards", HTTP_GET, [this] { handleGetDashboards(); });
  server->on("/api/dashboards", HTTP_POST, [this] { handlePostDashboards(); });
  server->on("/api/todo", HTTP_GET, [this] { handleGetTodo(); });
  server->on("/api/todo", HTTP_POST, [this] { handlePostTodo(); });

  server->onNotFound([this] { handleNotFound(); });
  LOG_DBG("WEB", "[MEM] Free heap after route setup: %d bytes", ESP.getFreeHeap());

  // Collect WebDAV headers and register handler
  // If-None-Match is collected so the static-page handlers can answer conditional GETs with 304
  const char* collectedHeaders[] = {"Depth",      "Destination", "Overwrite",    "If",
                                    "Lock-Token", "Timeout",     "If-None-Match"};
  server->collectHeaders(collectedHeaders, 7);
  server->addHandler(new WebDAVHandler());  // Note: WebDAVHandler will be deleted by WebServer when server is stopped
  LOG_DBG("WEB", "WebDAV handler initialized");

  server->begin();

  // Start WebSocket server for fast binary uploads
  LOG_DBG("WEB", "Starting WebSocket server on port %d...", wsPort);
  wsServer.reset(new WebSocketsServer(wsPort));
  wsInstance = const_cast<CrossPointWebServer*>(this);
  wsServer->begin();
  wsServer->onEvent(wsEventCallback);
  LOG_DBG("WEB", "WebSocket server started");

  udpActive = udp.begin(LOCAL_UDP_PORT);
  LOG_DBG("WEB", "Discovery UDP %s on port %d", udpActive ? "enabled" : "failed", LOCAL_UDP_PORT);

  // Do not subscribe the serving task to the task watchdog. Arduino WebServer
  // permits five-second client and ACK waits, which can consume the entire
  // default watchdog window on a weak connection. The interrupt watchdog still
  // catches hard CPU lockups, matching the rest of the application lifecycle.

  running = true;

  LOG_DBG("WEB", "Web server started on port %d", port);
  // Show the correct IP based on network mode
  const String ipAddr = apMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
  LOG_DBG("WEB", "Access at http://%s/", ipAddr.c_str());
  LOG_DBG("WEB", "WebSocket at ws://%s:%d/", ipAddr.c_str(), wsPort);
  LOG_DBG("WEB", "[MEM] Free heap after server.begin(): %d bytes", ESP.getFreeHeap());
}

void CrossPointWebServer::abortWsUpload(const char* tag) {
  // Explicit close() required: file-scope global persists beyond function scope
  wsUploadFile.close();
  String filePath = wsUploadPath;
  if (!filePath.endsWith("/")) filePath += "/";
  filePath += wsUploadFileName;
  if (Storage.remove(filePath.c_str())) {
    LOG_DBG(tag, "Deleted incomplete upload: %s", filePath.c_str());
  } else {
    LOG_DBG(tag, "Failed to delete incomplete upload: %s", filePath.c_str());
  }
  wsUploadInProgress = false;
  wsUploadClientNum = 255;
  wsLastProgressSent = 0;
}

void CrossPointWebServer::stop() {
  if (!running || !server) {
    LOG_DBG("WEB", "stop() called but already stopped (running=%d, server=%p)", running, server.get());
    return;
  }

  LOG_DBG("WEB", "STOP INITIATED - setting running=false first");
  running = false;  // Set this FIRST to prevent handleClient from using server

  LOG_DBG("WEB", "[MEM] Free heap before stop: %d bytes", ESP.getFreeHeap());

  // Close any in-progress WebSocket upload and remove partial file
  if (wsUploadInProgress && wsUploadFile) {
    abortWsUpload("WEB");
  }

  // Stop WebSocket server
  if (wsServer) {
    LOG_DBG("WEB", "Stopping WebSocket server...");
    wsServer->close();
    wsServer.reset();
    wsInstance = nullptr;
    LOG_DBG("WEB", "WebSocket server stopped");
  }

  if (udpActive) {
    udp.stop();
    udpActive = false;
  }

  // Brief delay to allow any in-flight handleClient() calls to complete
  delay(20);

  server->stop();
  LOG_DBG("WEB", "[MEM] Free heap after server->stop(): %d bytes", ESP.getFreeHeap());

  // Brief delay before deletion
  delay(10);

  server.reset();
  LOG_DBG("WEB", "Web server stopped and deleted");
  LOG_DBG("WEB", "[MEM] Free heap after delete server: %d bytes", ESP.getFreeHeap());

  // Note: Static upload variables (uploadFileName, uploadPath, uploadError) are declared
  // later in the file and will be cleared when they go out of scope or on next upload
  LOG_DBG("WEB", "[MEM] Free heap final: %d bytes", ESP.getFreeHeap());
}

void CrossPointWebServer::handleClient() {
  static unsigned long lastDebugPrint = 0;

  // Check running flag FIRST before accessing server
  if (!running) {
    return;
  }

  // Double-check server pointer is valid
  if (!server) {
    LOG_DBG("WEB", "WARNING: handleClient called with null server!");
    return;
  }

  // Print debug every 10 seconds to confirm handleClient is being called
  if (millis() - lastDebugPrint > 10000) {
    LOG_DBG("WEB", "handleClient active, server running on port %d", port);
    lastDebugPrint = millis();
  }

  server->handleClient();

  // Handle WebSocket events
  if (wsServer) {
    wsServer->loop();
  }

  // Respond to discovery broadcasts
  if (udpActive) {
    int packetSize = udp.parsePacket();
    if (packetSize > 0) {
      char buffer[16];
      int len = udp.read(buffer, sizeof(buffer) - 1);
      if (len > 0) {
        buffer[len] = '\0';
        if (strcmp(buffer, "hello") == 0) {
          String hostname = WiFi.getHostname();
          if (hostname.isEmpty()) {
            hostname = "crosspoint";
          }
          String message = "crosspoint (on " + hostname + ");" + String(wsPort);
          udp.beginPacket(udp.remoteIP(), udp.remotePort());
          udp.write(reinterpret_cast<const uint8_t*>(message.c_str()), message.length());
          udp.endPacket();
        }
      }
    }
  }
}

CrossPointWebServer::WsUploadStatus CrossPointWebServer::getWsUploadStatus() const {
  WsUploadStatus status;
  status.inProgress = wsUploadInProgress;
  status.received = wsUploadReceived;
  status.total = wsUploadSize;
  status.filename = wsUploadFileName.c_str();
  status.lastCompleteName = wsLastCompleteName.c_str();
  status.lastCompleteSize = wsLastCompleteSize;
  status.lastCompleteAt = wsLastCompleteAt;
  return status;
}

void CrossPointWebServer::handleRoot() const {
  PhonePage::sendStatic(*server, HomePageHtml, sizeof(HomePageHtml), HomePageHtmlETag, "text/html");
  LOG_DBG("WEB", "Served root page");
}

void CrossPointWebServer::handleJszip() const {
  PhonePage::sendStatic(*server, jszip_minJs, jszip_minJsCompressedSize, jszip_minJsETag,
                        "application/javascript");
  LOG_DBG("WEB", "Served jszip.min.js");
}

void CrossPointWebServer::handleAppCss() const {
  PhonePage::sendStatic(*server, appCss, appCssCompressedSize, appCssETag, "text/css");
}

void CrossPointWebServer::handleAppJs() const {
  PhonePage::sendStatic(*server, appJs, appJsCompressedSize, appJsETag, "application/javascript");
}

void CrossPointWebServer::handleNotFound() const {
  // CORS preflight: routes are registered per-method, so OPTIONS requests land
  // here. The Access-Control-Allow-* headers are added by enableCORS().
  if (server->method() == HTTP_OPTIONS) {
    server->send(204, "text/plain", "");
    return;
  }

  // in AP mode, redirect unmatched browser/captive-portal requests to "/" so the OS auto-opens the browser
  // API requests (/api/*) still return 404 so XHR errors surface correctly
  if (apMode && PhonePage::redirectCaptive(*server)) return;

  String message = "404 Not Found\n\n";
  message += "URI: " + server->uri() + "\n";
  server->send(404, "text/plain", message);
}

void CrossPointWebServer::handleStatus() const {
  // Get correct IP based on AP vs STA mode
  const String ipAddr = apMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString();

  JsonDocument doc;
  doc["version"] = CROSSPOINT_VERSION;
  doc["ip"] = ipAddr;
  doc["mode"] = apMode ? "AP" : "STA";
  doc["rssi"] = apMode ? 0 : WiFi.RSSI();
  doc["freeHeap"] = ESP.getFreeHeap();
  doc["uptime"] = millis() / 1000;
#if FREEINK_DEVICE_X4 || FREEINK_DEVICE_X3
  doc["device"] = gpio.deviceIsX3() ? "X3" : "X4";
#else
  doc["device"] = BoardConfig::ACTIVE.name;
#endif

  char snBuf[33] = {0};
  bool valid = false;
#if !CONFIG_IDF_TARGET_ESP32
  // Classic ESP32's efuse table has no USER_DATA block (C3/S3 only)
  if (esp_efuse_read_field_blob(ESP_EFUSE_USER_DATA, snBuf, 256) == ESP_OK) {
    valid = snBuf[0] != '\0' && snBuf[0] != (char)0xFF;
    for (int i = 0; i < 32 && snBuf[i] != '\0'; i++) {
      if (!std::isprint(static_cast<unsigned char>(snBuf[i]))) {
        valid = false;
        break;
      }
    }
  }
#endif

  if (valid) {
    doc["serial"] = snBuf;
  } else {
    doc["serial"] = "Not found";
  }

  String response;
  serializeJson(doc, response);
  server->send(200, "application/json", response);
}

void CrossPointWebServer::scanFiles(const char* path, const std::function<void(FileInfo)>& callback) const {
  HalFile root = Storage.open(path);
  if (!root) {
    LOG_DBG("WEB", "Failed to open directory: %s", path);
    return;
  }

  if (!root.isDirectory()) {
    LOG_DBG("WEB", "Not a directory: %s", path);
    root.close();
    return;
  }

  LOG_DBG("WEB", "Scanning files in: %s", path);

  HalFile file = root.openNextFile();
  char name[500];
  while (file) {
    file.getName(name, sizeof(name));
    auto fileName = String(name);

    // Skip hidden items (starting with ".")
    bool shouldHide = !SETTINGS.showHiddenFiles && fileName.startsWith(".");

    // Check against explicitly hidden items list
    if (!shouldHide) {
      for (const auto* item : HIDDEN_ITEMS) {
        if (fileName.equals(item)) {
          shouldHide = true;
          break;
        }
      }
    }

    if (!shouldHide) {
      FileInfo info;
      info.name = fileName;
      info.isDirectory = file.isDirectory();

      if (info.isDirectory) {
        info.size = 0;
        info.isEpub = false;
      } else {
        info.size = file.size();
        info.isEpub = isEpubFile(info.name);
      }

      callback(info);
    }

    file.close();
    yield();                          // Yield to allow WiFi and other tasks to process during long scans
    resetTaskWatchdogIfSubscribed();  // Reset watchdog to prevent timeout on large directories
    file = root.openNextFile();
  }
  root.close();
}

bool CrossPointWebServer::isEpubFile(const String& filename) const { return FsHelpers::hasEpubExtension(filename); }

void CrossPointWebServer::handleFileList() const {
  PhonePage::sendStatic(*server, FilesPageHtml, sizeof(FilesPageHtml), FilesPageHtmlETag, "text/html");
}

void CrossPointWebServer::handleFileListData() const {
  // Get current path from query string (default to root)
  String currentPath = "/";
  if (server->hasArg("path")) {
    currentPath = normalizeWebPath(server->arg("path"));
  }

  server->setContentLength(CONTENT_LENGTH_UNKNOWN);
  server->send(200, "application/json", "");
  server->sendContent("[");
  char output[512];
  constexpr size_t outputSize = sizeof(output);
  bool seenFirst = false;
  JsonDocument doc;

  scanFiles(currentPath.c_str(), [this, &output, &doc, seenFirst](const FileInfo& info) mutable {
    doc.clear();
    doc["name"] = info.name;
    doc["size"] = info.size;
    doc["isDirectory"] = info.isDirectory;
    doc["isEpub"] = info.isEpub;

    const size_t written = serializeJson(doc, output, outputSize);
    if (written >= outputSize) {
      // JSON output truncated; skip this entry to avoid sending malformed JSON
      LOG_DBG("WEB", "Skipping file entry with oversized JSON for name: %s", info.name.c_str());
      return;
    }

    if (seenFirst) {
      server->sendContent(",");
    } else {
      seenFirst = true;
    }
    server->sendContent(output);
  });
  server->sendContent("]");
  // End of streamed response, empty chunk to signal client
  server->sendContent("");
  LOG_DBG("WEB", "Served file listing page for path: %s", currentPath.c_str());
}

void CrossPointWebServer::handleDownload() const {
  if (!server->hasArg("path")) {
    server->send(400, "text/plain", "Missing path");
    return;
  }

  String itemPath = normalizeWebPath(server->arg("path"));
  if (itemPath.isEmpty() || itemPath == "/") {
    server->send(400, "text/plain", "Invalid path");
    return;
  }

  const String itemName = itemPath.substring(itemPath.lastIndexOf('/') + 1);
  if (itemName.startsWith(".")) {
    server->send(403, "text/plain", "Cannot access system files");
    return;
  }
  for (const auto* item : HIDDEN_ITEMS) {
    if (itemName.equals(item)) {
      server->send(403, "text/plain", "Cannot access protected items");
      return;
    }
  }

  if (!Storage.exists(itemPath.c_str())) {
    server->send(404, "text/plain", "Item not found");
    return;
  }

  HalFile file = Storage.open(itemPath.c_str());
  if (!file) {
    server->send(500, "text/plain", "Failed to open file");
    return;
  }
  if (file.isDirectory()) {
    file.close();
    server->send(400, "text/plain", "Path is a directory");
    return;
  }

  String contentType = "application/octet-stream";
  if (isEpubFile(itemPath)) {
    contentType = "application/epub+zip";
  }

  char nameBuf[128] = {0};
  String filename = "download";
  if (file.getName(nameBuf, sizeof(nameBuf))) {
    filename = nameBuf;
  }

  server->setContentLength(file.size());
  server->sendHeader("Content-Disposition", "attachment; filename=\"" + filename + "\"");
  server->send(200, contentType.c_str(), "");

  NetworkClient client = server->client();
  const size_t chunkSize = 4096;
  uint8_t buffer[chunkSize];

  bool downloadOk = true;
  while (downloadOk && file.available()) {
    int result = file.read(buffer, chunkSize);
    if (result <= 0) break;
    size_t bytesRead = static_cast<size_t>(result);
    size_t totalWritten = 0;
    while (totalWritten < bytesRead) {
      resetTaskWatchdogIfSubscribed();
      size_t wrote = client.write(buffer + totalWritten, bytesRead - totalWritten);
      if (wrote == 0) {
        downloadOk = false;
        break;
      }
      totalWritten += wrote;
    }
  }
  client.clear();
  file.close();
}

// Diagnostic counters for upload performance analysis
static unsigned long uploadStartTime = 0;
static unsigned long totalWriteTime = 0;
static size_t writeCount = 0;

static bool flushUploadBuffer(CrossPointWebServer::UploadState& state) {
  if (state.bufferPos > 0 && state.file) {
    resetTaskWatchdogIfSubscribed();  // Reset watchdog before potentially slow SD write
    const unsigned long writeStart = millis();
    const size_t written = state.file.write(state.buffer.data(), state.bufferPos);
    totalWriteTime += millis() - writeStart;
    writeCount++;
    resetTaskWatchdogIfSubscribed();  // Reset watchdog after SD write

    if (written != state.bufferPos) {
      LOG_DBG("WEB", "[UPLOAD] Buffer flush failed: expected %d, wrote %d", state.bufferPos, written);
      state.bufferPos = 0;
      return false;
    }
    state.bufferPos = 0;
  }
  return true;
}

void CrossPointWebServer::handleUpload(UploadState& state) const {
  static size_t lastLoggedSize = 0;

  // Reset watchdog at start of every upload callback - HTTP parsing can be slow
  resetTaskWatchdogIfSubscribed();

  // Safety check: ensure server is still valid
  if (!running || !server) {
    LOG_DBG("WEB", "[UPLOAD] ERROR: handleUpload called but server not running!");
    return;
  }

  const HTTPUpload& upload = server->upload();

  if (upload.status == UPLOAD_FILE_START) {
    // Reset watchdog - this is the critical 1% crash point
    resetTaskWatchdogIfSubscribed();

    state.fileName = upload.filename;
    state.size = 0;
    state.success = false;
    state.error = "";
    uploadStartTime = millis();
    lastLoggedSize = 0;
    state.bufferPos = 0;
    totalWriteTime = 0;
    writeCount = 0;

    if (!FsHelpers::isSafePathComponent(state.fileName)) {
      state.error = "Invalid file name";
      LOG_DBG("WEB", "[UPLOAD] Rejected unsafe filename: %s", state.fileName.c_str());
      return;
    }

    // Get upload path from query parameter (defaults to root if not specified)
    // Note: We use query parameter instead of form data because multipart form
    // fields aren't available until after file upload completes
    if (server->hasArg("path")) {
      state.path = normalizeWebPath(server->arg("path"));
    } else {
      state.path = "/";
    }

    LOG_DBG("WEB", "[UPLOAD] START: %s to path: %s", state.fileName.c_str(), state.path.c_str());
    LOG_DBG("WEB", "[UPLOAD] Free heap: %d bytes", ESP.getFreeHeap());

    String filePath = state.path;
    if (!filePath.endsWith("/")) filePath += "/";
    filePath += state.fileName;

    // Check if file already exists - SD operations can be slow
    resetTaskWatchdogIfSubscribed();
    if (Storage.exists(filePath.c_str())) {
      state.error = "File already exists: " + state.fileName;
      LOG_DBG("WEB", "[UPLOAD] Collision: %s", filePath.c_str());
      return;
    }

    // Open file for writing - this can be slow due to FAT cluster allocation
    resetTaskWatchdogIfSubscribed();
    if (!Storage.openFileForWrite("WEB", filePath, state.file)) {
      state.error = "Failed to create file on SD card";
      LOG_DBG("WEB", "[UPLOAD] FAILED to create file: %s", filePath.c_str());
      return;
    }
    resetTaskWatchdogIfSubscribed();

    LOG_DBG("WEB", "[UPLOAD] File created successfully: %s", filePath.c_str());
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (state.file && state.error.isEmpty()) {
      // Buffer incoming data and flush when buffer is full
      // This reduces SD card write operations and improves throughput
      const uint8_t* data = upload.buf;
      size_t remaining = upload.currentSize;

      while (remaining > 0) {
        const size_t space = UploadState::UPLOAD_BUFFER_SIZE - state.bufferPos;
        const size_t toCopy = (remaining < space) ? remaining : space;

        memcpy(state.buffer.data() + state.bufferPos, data, toCopy);
        state.bufferPos += toCopy;
        data += toCopy;
        remaining -= toCopy;

        // Flush buffer when full
        if (state.bufferPos >= UploadState::UPLOAD_BUFFER_SIZE) {
          if (!flushUploadBuffer(state)) {
            state.error = "Failed to write to SD card - disk may be full";
            state.file.close();
            return;
          }
        }
      }

      state.size += upload.currentSize;

      // Log progress every 100KB
      if (state.size - lastLoggedSize >= 102400) {
        const unsigned long elapsed = millis() - uploadStartTime;
        const float kbps = (elapsed > 0) ? (state.size / 1024.0) / (elapsed / 1000.0) : 0;
        LOG_DBG("WEB", "[UPLOAD] %d bytes (%.1f KB), %.1f KB/s, %d writes", state.size, state.size / 1024.0, kbps,
                writeCount);
        lastLoggedSize = state.size;
      }
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (state.file) {
      // Flush any remaining buffered data
      if (!flushUploadBuffer(state)) {
        state.error = "Failed to write final data to SD card";
      }
      state.file.close();

      if (state.error.isEmpty()) {
        state.success = true;
        const unsigned long elapsed = millis() - uploadStartTime;
        const float avgKbps = (elapsed > 0) ? (state.size / 1024.0) / (elapsed / 1000.0) : 0;
        const float writePercent = (elapsed > 0) ? (totalWriteTime * 100.0 / elapsed) : 0;
        LOG_DBG("WEB", "[UPLOAD] Complete: %s (%d bytes in %lu ms, avg %.1f KB/s)", state.fileName.c_str(), state.size,
                elapsed, avgKbps);
        LOG_DBG("WEB", "[UPLOAD] Diagnostics: %d writes, total write time: %lu ms (%.1f%%)", writeCount, totalWriteTime,
                writePercent);

        // Clear epub cache after uploading the file
        String filePath = state.path;
        if (!filePath.endsWith("/")) filePath += "/";
        filePath += state.fileName;
        clearBookCache(filePath.c_str());
        if (isLibraryBookFile(state.fileName)) library::markLibraryIndexDirty();
      }
    }
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    state.bufferPos = 0;  // Discard buffered data
    if (state.file) {
      state.file.close();
      // Try to delete the incomplete file
      String filePath = state.path;
      if (!filePath.endsWith("/")) filePath += "/";
      filePath += state.fileName;
      Storage.remove(filePath.c_str());
    }
    state.error = "Upload aborted";
    LOG_DBG("WEB", "Upload aborted");
  }
}

void CrossPointWebServer::handleUploadPost(UploadState& state) const {
  if (state.success) {
    server->send(200, "text/plain", "File uploaded successfully: " + state.fileName);
  } else {
    const String error = state.error.isEmpty() ? "Unknown error during upload" : state.error;
    server->send(400, "text/plain", error);
  }
}

void CrossPointWebServer::handleCreateFolder() const {
  // Get folder name from form data
  if (!server->hasArg("name")) {
    server->send(400, "text/plain", "Missing folder name");
    return;
  }

  const String folderName = server->arg("name");

  // Validate folder name
  if (folderName.isEmpty()) {
    server->send(400, "text/plain", "Folder name cannot be empty");
    return;
  }
  if (!FsHelpers::isSafePathComponent(folderName)) {
    LOG_DBG("WEB", "Rejected unsafe folder name: %s", folderName.c_str());
    server->send(400, "text/plain", "Invalid folder name");
    return;
  }
  if (isProtectedItemName(folderName)) {
    LOG_DBG("WEB", "Rejected protected folder name: %s", folderName.c_str());
    server->send(403, "text/plain", "Cannot create protected item");
    return;
  }

  // Get parent path
  String parentPath = "/";
  if (server->hasArg("path")) {
    parentPath = normalizeWebPath(server->arg("path"));
  }

  // Build full folder path
  String folderPath = parentPath;
  if (!folderPath.endsWith("/")) folderPath += "/";
  folderPath += folderName;

  LOG_DBG("WEB", "Creating folder: %s", folderPath.c_str());

  // Check if already exists
  if (Storage.exists(folderPath.c_str())) {
    server->send(400, "text/plain", "Folder already exists");
    return;
  }

  // Create the folder
  if (Storage.mkdir(folderPath.c_str())) {
    LOG_DBG("WEB", "Folder created successfully: %s", folderPath.c_str());
    server->send(200, "text/plain", "Folder created: " + folderName);
  } else {
    LOG_DBG("WEB", "Failed to create folder: %s", folderPath.c_str());
    server->send(500, "text/plain", "Failed to create folder");
  }
}

void CrossPointWebServer::handleRename() const {
  if (!server->hasArg("path") || !server->hasArg("name")) {
    server->send(400, "text/plain", "Missing path or new name");
    return;
  }

  String itemPath = normalizeWebPath(server->arg("path"));
  String newName = server->arg("name");
  newName.trim();

  if (itemPath.isEmpty() || itemPath == "/") {
    server->send(400, "text/plain", "Invalid path");
    return;
  }
  if (newName.isEmpty()) {
    server->send(400, "text/plain", "New name cannot be empty");
    return;
  }
  if (newName.indexOf('/') >= 0 || newName.indexOf('\\') >= 0) {
    server->send(400, "text/plain", "Invalid file name");
    return;
  }
  if (isProtectedItemName(newName)) {
    server->send(403, "text/plain", "Cannot rename to protected name");
    return;
  }

  const String itemName = itemPath.substring(itemPath.lastIndexOf('/') + 1);
  if (isProtectedItemName(itemName)) {
    server->send(403, "text/plain", "Cannot rename protected item");
    return;
  }
  if (newName == itemName) {
    server->send(200, "text/plain", "Name unchanged");
    return;
  }

  if (!Storage.exists(itemPath.c_str())) {
    server->send(404, "text/plain", "Item not found");
    return;
  }

  HalFile file = Storage.open(itemPath.c_str());
  if (!file) {
    server->send(500, "text/plain", "Failed to open file");
    return;
  }
  if (file.isDirectory()) {
    file.close();
    server->send(400, "text/plain", "Only files can be renamed");
    return;
  }

  String parentPath = itemPath.substring(0, itemPath.lastIndexOf('/'));
  if (parentPath.isEmpty()) {
    parentPath = "/";
  }
  String newPath = parentPath;
  if (!newPath.endsWith("/")) {
    newPath += "/";
  }
  newPath += newName;

  if (Storage.exists(newPath.c_str())) {
    file.close();
    server->send(409, "text/plain", "Target already exists");
    return;
  }

  clearBookCache(itemPath.c_str());
  const bool success = file.rename(newPath.c_str());
  file.close();

  if (success) {
    LOG_DBG("WEB", "Renamed file: %s -> %s", itemPath.c_str(), newPath.c_str());
    server->send(200, "text/plain", "Renamed successfully");
  } else {
    LOG_ERR("WEB", "Failed to rename file: %s -> %s", itemPath.c_str(), newPath.c_str());
    server->send(500, "text/plain", "Failed to rename file");
  }
}

void CrossPointWebServer::handleMove() const {
  if (!server->hasArg("path") || !server->hasArg("dest")) {
    server->send(400, "text/plain", "Missing path or destination");
    return;
  }

  String itemPath = normalizeWebPath(server->arg("path"));
  String destPath = normalizeWebPath(server->arg("dest"));

  if (itemPath.isEmpty() || itemPath == "/") {
    server->send(400, "text/plain", "Invalid path");
    return;
  }
  if (destPath.isEmpty()) {
    server->send(400, "text/plain", "Invalid destination");
    return;
  }

  const String itemName = itemPath.substring(itemPath.lastIndexOf('/') + 1);
  if (isProtectedItemName(itemName)) {
    server->send(403, "text/plain", "Cannot move protected item");
    return;
  }
  if (destPath != "/") {
    const String destName = destPath.substring(destPath.lastIndexOf('/') + 1);
    if (isProtectedItemName(destName)) {
      server->send(403, "text/plain", "Cannot move into protected folder");
      return;
    }
  }

  if (!Storage.exists(itemPath.c_str())) {
    server->send(404, "text/plain", "Item not found");
    return;
  }

  HalFile file = Storage.open(itemPath.c_str());
  if (!file) {
    server->send(500, "text/plain", "Failed to open file");
    return;
  }
  if (file.isDirectory()) {
    file.close();
    server->send(400, "text/plain", "Only files can be moved");
    return;
  }

  if (!Storage.exists(destPath.c_str())) {
    file.close();
    server->send(404, "text/plain", "Destination not found");
    return;
  }
  HalFile destDir = Storage.open(destPath.c_str());
  if (!destDir || !destDir.isDirectory()) {
    if (destDir) {
      destDir.close();
    }
    file.close();
    server->send(400, "text/plain", "Destination is not a folder");
    return;
  }
  destDir.close();

  String newPath = destPath;
  if (!newPath.endsWith("/")) {
    newPath += "/";
  }
  newPath += itemName;

  if (newPath == itemPath) {
    file.close();
    server->send(200, "text/plain", "Already in destination");
    return;
  }
  if (Storage.exists(newPath.c_str())) {
    file.close();
    server->send(409, "text/plain", "Target already exists");
    return;
  }

  clearBookCache(itemPath.c_str());
  const bool success = file.rename(newPath.c_str());
  file.close();

  if (success) {
    LOG_DBG("WEB", "Moved file: %s -> %s", itemPath.c_str(), newPath.c_str());
    server->send(200, "text/plain", "Moved successfully");
  } else {
    LOG_ERR("WEB", "Failed to move file: %s -> %s", itemPath.c_str(), newPath.c_str());
    server->send(500, "text/plain", "Failed to move file");
  }
}

void CrossPointWebServer::handleDelete() const {
  // To ensure backwards compatibility, plain `path` is mapped
  // to a single element JSON array.
  bool hasPathArg = server->hasArg("path");
  bool hasPathsArg = server->hasArg("paths");
  // Check 'paths' or `path` argument is provided
  if (!(hasPathArg || hasPathsArg)) {
    server->send(400, "text/plain", "Missing `path` or `paths` argument");
    return;
  }
  if (hasPathArg && hasPathsArg) {
    server->send(400, "text/plain", "Provide either 'path' or 'paths', not both");
    return;
  }

  // Parse paths
  String pathsArg;
  JsonDocument doc;
  DeserializationError error = DeserializationError(DeserializationError::Code::Ok);
  if (hasPathsArg) {
    pathsArg = server->arg("paths");
    error = deserializeJson(doc, pathsArg);
  } else {
    pathsArg = server->arg("path");
    doc.add(pathsArg);
  }
  if (error) {
    server->send(400, "text/plain", "Invalid paths format");
    return;
  }

  auto paths = doc.as<JsonArray>();
  if (paths.isNull() || paths.size() == 0) {
    server->send(400, "text/plain", "No paths provided");
    return;
  }

  // Iterate over paths and delete each item
  bool allSuccess = true;
  String failedItems;

  for (const auto& p : paths) {
    auto itemPath = normalizeWebPath(p.as<String>());

    // Validate path
    if (itemPath.isEmpty() || itemPath == "/") {
      failedItems += itemPath + " (cannot delete root); ";
      allSuccess = false;
      continue;
    }

    // Security check: prevent deletion of protected items
    const String itemName = itemPath.substring(itemPath.lastIndexOf('/') + 1);

    // Hidden/system files are protected
    if (itemName.startsWith(".")) {
      failedItems += itemPath + " (hidden/system file); ";
      allSuccess = false;
      continue;
    }

    // Check against explicitly protected items
    bool isProtected = false;
    for (const auto* item : HIDDEN_ITEMS) {
      if (itemName.equals(item)) {
        isProtected = true;
        break;
      }
    }
    if (isProtected) {
      failedItems += itemPath + " (protected file); ";
      allSuccess = false;
      continue;
    }

    // Check if item exists
    if (!Storage.exists(itemPath.c_str())) {
      failedItems += itemPath + " (not found); ";
      allSuccess = false;
      continue;
    }

    // Decide whether it's a directory or file by opening it
    bool success = false;
    HalFile f = Storage.open(itemPath.c_str());
    if (f && f.isDirectory()) {
      // For folders, ensure empty before removing
      HalFile entry = f.openNextFile();
      if (entry) {
        entry.close();
        f.close();
        failedItems += itemPath + " (folder not empty); ";
        allSuccess = false;
        continue;
      }
      f.close();
      success = Storage.rmdir(itemPath.c_str());
    } else {
      // It's a file (or couldn't open as dir) — remove file
      if (f) f.close();
      success = Storage.remove(itemPath.c_str());
      clearBookCache(itemPath.c_str());
    }

    if (!success) {
      failedItems += itemPath + " (deletion failed); ";
      allSuccess = false;
    }
  }

  if (allSuccess) {
    server->send(200, "text/plain", "All items deleted successfully");
  } else {
    server->send(500, "text/plain", "Failed to delete some items: " + failedItems);
  }
}

void CrossPointWebServer::handleSettingsPage() const {
  PhonePage::sendStatic(*server, SettingsPageHtml, sizeof(SettingsPageHtml), SettingsPageHtmlETag, "text/html");
  LOG_DBG("WEB", "Served settings page");
}

void CrossPointWebServer::handleGetSettings() const {
  // Pass the SD font registry so the fontFamily setting's enumStringValues
  // includes SD-resident families — otherwise the web API only exposes the
  // three built-in fonts.
  const auto& settings = getSettingsList(&sdFontSystem.registry());

  server->setContentLength(CONTENT_LENGTH_UNKNOWN);
  server->send(200, "application/json", "");
  server->sendContent("[");

  char output[512];
  constexpr size_t outputSize = sizeof(output);
  bool seenFirst = false;
  JsonDocument doc;

  for (const auto& s : settings) {
    if (!s.key) continue;  // Skip ACTION-only entries

    doc.clear();
    doc["key"] = s.key;
    doc["name"] = I18N.get(s.nameId);
    doc["category"] = I18N.get(s.category);

    switch (s.type) {
      case SettingType::TOGGLE: {
        doc["type"] = "toggle";
        if (s.valuePtr) {
          doc["value"] = static_cast<int>(SETTINGS.*(s.valuePtr));
        }
        break;
      }
      case SettingType::ENUM: {
        doc["type"] = "enum";
        if (s.valuePtr) {
          doc["value"] = static_cast<int>(SETTINGS.*(s.valuePtr));
        } else if (s.valueGetter) {
          doc["value"] = static_cast<int>(s.valueGetter());
        }
        JsonArray options = doc["options"].to<JsonArray>();
        if (!s.enumStringValues.empty()) {
          for (const auto& opt : s.enumStringValues) {
            options.add(opt);
          }
        } else {
          for (const auto& opt : s.enumLabels()) {
            options.add(I18N.get(opt));
          }
        }
        break;
      }
      case SettingType::VALUE: {
        doc["type"] = "value";
        if (s.valuePtr) {
          doc["value"] = static_cast<int>(SETTINGS.*(s.valuePtr));
        }
        doc["min"] = s.valueRange.min;
        doc["max"] = s.valueRange.max;
        doc["step"] = s.valueRange.step;
        break;
      }
      case SettingType::STRING: {
        doc["type"] = "string";
        if (s.stringGetter) {
          doc["value"] = s.stringGetter();
        } else if (s.stringMaxLen > 0) {
          doc["value"] = reinterpret_cast<const char*>(&SETTINGS) + s.stringOffset;
        }
        break;
      }
      default:
        continue;
    }

    const size_t written = serializeJson(doc, output, outputSize);
    if (written >= outputSize) {
      LOG_DBG("WEB", "Skipping oversized setting JSON for: %s", s.key);
      continue;
    }

    if (seenFirst) {
      server->sendContent(",");
    } else {
      seenFirst = true;
    }
    server->sendContent(output);
    yield();                          // Yield to allow WiFi and other tasks to process during a slow send
    resetTaskWatchdogIfSubscribed();  // Reset watchdog: each sendContent() is a blocking network write
  }

  server->sendContent("]");
  server->sendContent("");
  LOG_DBG("WEB", "Served settings API");
}

void CrossPointWebServer::handlePostSettings() {
  if (!server->hasArg("plain")) {
    server->send(400, "text/plain", "Missing JSON body");
    return;
  }

  const String body = server->arg("plain");
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, body);
  if (err) {
    server->send(400, "text/plain", String("Invalid JSON: ") + err.c_str());
    return;
  }

  const auto& settings = getSettingsList(&sdFontSystem.registry());
  int applied = 0;

  for (const auto& s : settings) {
    if (!s.key) continue;
    if (!doc[s.key].is<JsonVariant>()) continue;

    switch (s.type) {
      case SettingType::TOGGLE: {
        const int val = doc[s.key].as<int>() ? 1 : 0;
        if (s.valuePtr) {
          SETTINGS.*(s.valuePtr) = val;
        }
        applied++;
        break;
      }
      case SettingType::ENUM: {
        const int val = doc[s.key].as<int>();
        const int maxVal = s.enumStringValues.empty() ? static_cast<int>(s.enumLabels().size())
                                                      : static_cast<int>(s.enumStringValues.size());
        if (val >= 0 && val < maxVal) {
          if (s.valuePtr) {
            SETTINGS.*(s.valuePtr) = static_cast<uint8_t>(val);
          } else if (s.valueSetter) {
            s.valueSetter(static_cast<uint8_t>(val));
          }
          applied++;
        }
        break;
      }
      case SettingType::VALUE: {
        const int val = doc[s.key].as<int>();
        if (val >= s.valueRange.min && val <= s.valueRange.max) {
          if (s.valuePtr) {
            SETTINGS.*(s.valuePtr) = static_cast<uint8_t>(val);
          }
          applied++;
        }
        break;
      }
      case SettingType::STRING: {
        const std::string val = doc[s.key].as<std::string>();
        if (s.stringSetter) {
          s.stringSetter(val);
        } else if (s.stringMaxLen > 0) {
          char* ptr = reinterpret_cast<char*>(&SETTINGS) + s.stringOffset;
          strncpy(ptr, val.c_str(), s.stringMaxLen - 1);
          ptr[s.stringMaxLen - 1] = '\0';
        }
        applied++;
        break;
      }
      default:
        break;
    }
  }

  SETTINGS.saveToFile();

  LOG_DBG("WEB", "Applied %d setting(s)", applied);
  server->send(200, "text/plain", String("Applied ") + String(applied) + " setting(s)");
}

// ---- Dashboards API ----

void CrossPointWebServer::handleDashboardsPage() const {
  PhonePage::sendStatic(*server, DashboardsPageHtml, sizeof(DashboardsPageHtml), DashboardsPageHtmlETag, "text/html");
  LOG_DBG("WEB", "Served dashboards page");
}

void CrossPointWebServer::handlePicturesPage() const {
  PhonePage::sendStatic(*server, PicturesPageHtml, sizeof(PicturesPageHtml), PicturesPageHtmlETag, "text/html");
  LOG_DBG("WEB", "Served pictures page");
}

void CrossPointWebServer::handleGetDashboards() const {
  auto& config = DASHBOARD_CONFIG;
  config.loadFromFile();  // A missing file keeps the defaults

  JsonDocument doc;
  JsonObject layout = doc["layout"].to<JsonObject>();
  layout["active"] = config.activeLayout() ? config.activeLayoutId : 0;
  doc["orientation"] = SETTINGS.dashboardOrientation;

  JsonObject grid = doc["grid"].to<JsonObject>();
  grid["long"] = dashboard_layout::GRID_LONG;
  grid["short"] = dashboard_layout::GRID_SHORT;
  grid["minCells"] = dashboard_layout::MIN_TILE_CELLS;
  grid["maxTiles"] = CustomLayout::MAX_TILES;
  grid["maxName"] = static_cast<uint32_t>(CustomLayout::MAX_NAME_LENGTH);
  grid["maxLayouts"] = DashboardConfigStore::MAX_LAYOUTS;

  JsonArray layouts = doc["layouts"].to<JsonArray>();
  for (const auto& entry : config.layouts) entry.toJson(layouts.add<JsonObject>());

  JsonObject weather = doc["weather"].to<JsonObject>();
  weather["hasLocation"] = config.weatherHasLocation;
  weather["place"] = config.weatherPlace.c_str();
  weather["lat"] = roundCoordinate(config.weatherLatitude);
  weather["lon"] = roundCoordinate(config.weatherLongitude);
  weather["units"] = config.weatherFahrenheit ? "f" : "c";

  JsonObject carousel = doc["carousel"].to<JsonObject>();
  carousel["folder"] = config.carouselFolder.c_str();
  carousel["intervalMinutes"] = config.carouselIntervalMinutes;
  carousel["minMinutes"] = DashboardConfigStore::MIN_CAROUSEL_MINUTES;
  carousel["maxMinutes"] = DashboardConfigStore::MAX_CAROUSEL_MINUTES;

  String json;
  serializeJson(doc, json);
  server->send(200, "application/json", json);
  LOG_DBG("WEB", "Served dashboards API");
}

void CrossPointWebServer::handlePostDashboards() {
  if (!server->hasArg("plain")) {
    sendJsonError(*server, 400, "Missing JSON body");
    return;
  }

  const String body = server->arg("plain");
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, body);
  if (err) {
    sendJsonError(*server, 400, (String("Invalid JSON: ") + err.c_str()).c_str());
    return;
  }
  if (!doc.is<JsonObject>()) {
    sendJsonError(*server, 400, "Expected a JSON object");
    return;
  }

  auto& config = DASHBOARD_CONFIG;
  config.loadFromFile();  // A missing file keeps the defaults
  const DashboardSnapshot before(config);

  DashboardOutcome outcome;
  const char* badField = applyDashboardChanges(config, doc.as<JsonVariantConst>(), outcome);
  if (badField) {
    before.restore(config);
    LOG_DBG("WEB", "Rejected dashboard setting: %s", badField);
    sendJsonError(*server, 400, outcome.layoutsFull ? "No room for another layout" : "Invalid value", badField);
    return;
  }

  if (!config.saveToFile()) {
    before.restore(config);
    LOG_ERR("WEB", "Failed to save dashboard settings");
    sendJsonError(*server, 500, "Could not save to the SD card");
    return;
  }

  LOG_DBG("WEB", "Saved dashboard settings");
  JsonDocument answer;
  answer["ok"] = true;
  if (outcome.savedId) answer["id"] = outcome.savedId;
  if (outcome.restoreAsked) answer["restored"] = outcome.restored;
  String json;
  serializeJson(answer, json);
  server->send(200, "application/json", json);
}

// ---- To-do API ----
// The list lives on SD; each request loads it, acts, saves and unloads it, as
// the dashboard does around its own use.

void CrossPointWebServer::handleGetTodo() const {
  TODO_STORE.load();
  sendTodoList(*server);
  TODO_STORE.unload();
  LOG_DBG("WEB", "Served to-do API");
}

void CrossPointWebServer::handlePostTodo() {
  if (!server->hasArg("plain")) {
    sendJsonError(*server, 400, "Missing JSON body");
    return;
  }

  const String body = server->arg("plain");
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, body);
  if (err) {
    sendJsonError(*server, 400, (String("Invalid JSON: ") + err.c_str()).c_str());
    return;
  }

  const char* op = doc["op"] | "";
  const bool isAdd = strcmp(op, "add") == 0;
  const bool isEdit = strcmp(op, "edit") == 0;
  const bool isToggle = strcmp(op, "toggle") == 0;
  const bool isDelete = strcmp(op, "delete") == 0;
  const bool isClearDone = strcmp(op, "clearDone") == 0;
  if (!isAdd && !isEdit && !isToggle && !isDelete && !isClearDone) {
    sendJsonError(*server, 400, "Invalid value", "op");
    return;
  }

  // Text for add and edit: 1 to MAX_TEXT_LENGTH bytes of clean UTF-8. The page
  // trims it and cuts it on a character boundary, so the store's byte cut
  // never splits one.
  std::string text;
  if (isAdd || isEdit) {
    if (!doc["text"].is<const char*>()) {
      sendJsonError(*server, 400, "Invalid value", "text");
      return;
    }
    const JsonString value = doc["text"].as<JsonString>();
    if (value.size() == 0 || value.size() > TodoStore::MAX_TEXT_LENGTH || !isCleanText(value.c_str(), value.size())) {
      sendJsonError(*server, 400, "Invalid value", "text");
      return;
    }
    text.assign(value.c_str(), value.size());
  }
  const int index = doc["index"].is<int>() ? doc["index"].as<int>() : -1;
  const size_t position = index >= 0 ? static_cast<size_t>(index) : SIZE_MAX;

  auto& todo = TODO_STORE;
  todo.load();
  bool changed = true;
  const char* badField = nullptr;
  if (isAdd) {
    if (!todo.add(text)) badField = "items";
  } else if (isEdit) {
    if (!todo.setText(position, text)) badField = "index";
  } else if (isToggle) {
    if (!todo.toggle(position)) badField = "index";
  } else if (isDelete) {
    if (!todo.remove(position)) badField = "index";
  } else {
    changed = todo.removeDone();  // None done is not an error
  }

  if (badField) {
    todo.unload();
    sendJsonError(*server, 400, strcmp(badField, "items") == 0 ? "The list is full" : "Invalid value", badField);
    return;
  }
  if (changed && !todo.saveToFile()) {
    todo.unload();
    LOG_ERR("WEB", "Failed to save to-do list");
    sendJsonError(*server, 500, "Could not save to the SD card");
    return;
  }
  sendTodoList(*server);
  todo.unload();
  LOG_DBG("WEB", "To-do %s", op);
}

// ---- OPDS Server API ----

void CrossPointWebServer::handleGetOpdsServers() const {
  const auto& servers = OPDS_STORE.getServers();

  // Stream JSON array incrementally to avoid allocating the full response in memory
  server->setContentLength(CONTENT_LENGTH_UNKNOWN);
  server->send(200, "application/json", "");
  server->sendContent("[");

  char output[512];
  constexpr size_t outputSize = sizeof(output);
  JsonDocument doc;

  for (size_t i = 0; i < servers.size(); i++) {
    doc.clear();
    doc["index"] = i;
    doc["name"] = servers[i].name;
    doc["url"] = servers[i].url;
    doc["username"] = servers[i].username;
    // Never expose passwords over the API — only indicate whether one is set
    doc["hasPassword"] = !servers[i].password.empty();

    const size_t written = serializeJson(doc, output, outputSize);
    if (written >= outputSize) continue;

    if (i > 0) server->sendContent(",");
    server->sendContent(output);
    yield();                          // Yield to allow WiFi and other tasks to process during a slow send
    resetTaskWatchdogIfSubscribed();  // Reset watchdog: each sendContent() is a blocking network write
  }

  server->sendContent("]");
  server->sendContent("");
  LOG_DBG("WEB", "Served OPDS servers API (%zu servers)", servers.size());
}

void CrossPointWebServer::handlePostOpdsServer() {
  if (!server->hasArg("plain")) {
    server->send(400, "text/plain", "Missing JSON body");
    return;
  }

  const String body = server->arg("plain");
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, body);
  if (err) {
    server->send(400, "text/plain", String("Invalid JSON: ") + err.c_str());
    return;
  }

  OpdsServer opdsServer;
  opdsServer.name = doc["name"] | std::string("");
  opdsServer.url = doc["url"] | std::string("");
  opdsServer.username = doc["username"] | std::string("");

  // The password field is optional in the JSON payload. When absent (vs. present but empty),
  // we preserve the existing password — the web UI omits it when the user hasn't changed it.
  bool hasPasswordField = doc["password"].is<const char*>() || doc["password"].is<std::string>();
  std::string password = doc["password"] | std::string("");

  if (doc["index"].is<int>()) {
    int idx = doc["index"].as<int>();
    if (idx < 0 || idx >= static_cast<int>(OPDS_STORE.getCount())) {
      server->send(400, "text/plain", "Invalid server index");
      return;
    }
    // Preserve existing password if not explicitly provided
    if (!hasPasswordField) {
      const auto* existing = OPDS_STORE.getServer(static_cast<size_t>(idx));
      if (existing) password = existing->password;
    }
    opdsServer.password = password;
    OPDS_STORE.updateServer(static_cast<size_t>(idx), opdsServer);
    LOG_DBG("WEB", "Updated OPDS server at index %d", idx);
  } else {
    opdsServer.password = password;
    if (!OPDS_STORE.addServer(opdsServer)) {
      server->send(400, "text/plain", "Cannot add server (limit reached)");
      return;
    }
    LOG_DBG("WEB", "Added new OPDS server: %s", opdsServer.name.c_str());
  }

  server->send(200, "text/plain", "OK");
}

// Uses POST (not HTTP DELETE) because ESP32 WebServer doesn't support DELETE with body.
void CrossPointWebServer::handleDeleteOpdsServer() {
  if (!server->hasArg("plain")) {
    server->send(400, "text/plain", "Missing JSON body");
    return;
  }

  const String body = server->arg("plain");
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, body);
  if (err) {
    server->send(400, "text/plain", String("Invalid JSON: ") + err.c_str());
    return;
  }

  if (!doc["index"].is<int>()) {
    server->send(400, "text/plain", "Missing index");
    return;
  }

  int idx = doc["index"].as<int>();
  if (idx < 0 || idx >= static_cast<int>(OPDS_STORE.getCount())) {
    server->send(400, "text/plain", "Invalid server index");
    return;
  }

  OPDS_STORE.removeServer(static_cast<size_t>(idx));
  LOG_DBG("WEB", "Deleted OPDS server at index %d", idx);
  server->send(200, "text/plain", "OK");
}

// ---- Wi-Fi Credentials API ----

void CrossPointWebServer::handleGetWifiNetworks() const {
  const auto credentials = WIFI_STORE.getCredentialSummaries();

  // Stream JSON array incrementally to avoid allocating the full response in memory
  server->setContentLength(CONTENT_LENGTH_UNKNOWN);
  server->send(200, "application/json", "");
  server->sendContent("[");

  char output[320];
  constexpr size_t outputSize = sizeof(output);
  JsonDocument doc;

  for (size_t i = 0; i < credentials.size(); i++) {
    doc.clear();
    doc["index"] = i;
    doc["ssid"] = credentials[i].ssid;
    // Never expose Wi-Fi passwords over the API — only indicate whether one is set
    doc["hasPassword"] = credentials[i].hasPassword;
    doc["isLastConnected"] = credentials[i].isLastConnected;

    const size_t written = serializeJson(doc, output, outputSize);
    if (written >= outputSize) continue;

    if (i > 0) server->sendContent(",");
    server->sendContent(output);
    yield();                          // Yield to allow WiFi and other tasks to process during a slow send
    resetTaskWatchdogIfSubscribed();  // Reset watchdog: each sendContent() is a blocking network write
  }

  server->sendContent("]");
  server->sendContent("");
  LOG_DBG("WEB", "Served Wi-Fi credentials API (%zu network(s))", credentials.size());
}

void CrossPointWebServer::handlePostWifiNetwork() {
  if (!server->hasArg("plain")) {
    server->send(400, "text/plain", "Missing JSON body");
    return;
  }

  const String body = server->arg("plain");
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, body);
  if (err) {
    server->send(400, "text/plain", String("Invalid JSON: ") + err.c_str());
    return;
  }

  std::string ssid = doc["ssid"] | std::string("");
  if (ssid.empty()) {
    server->send(400, "text/plain", "SSID is required");
    return;
  }

  // The password field is optional in the JSON payload. When absent (vs. present but empty),
  // preserve the existing password for updates. Empty passwords are valid for open networks.
  bool hasPasswordField = doc["password"].is<const char*>() || doc["password"].is<std::string>();
  std::string password = doc["password"] | std::string("");

  if (doc["index"].is<int>()) {
    int idx = doc["index"].as<int>();
    if (idx < 0) {
      server->send(400, "text/plain", "Invalid network index");
      return;
    }
    const auto credential = WIFI_STORE.getCredentialAt(static_cast<size_t>(idx));
    if (!credential) {
      server->send(400, "text/plain", "Invalid network index");
      return;
    }

    const std::string oldSsid = credential->ssid;
    if (!hasPasswordField) {
      password = credential->password;
    }

    bool ok = true;
    if (oldSsid != ssid) {
      ok = WIFI_STORE.removeCredential(oldSsid) && WIFI_STORE.addCredential(ssid, password);
    } else {
      ok = WIFI_STORE.addCredential(ssid, password);
    }

    if (!ok) {
      server->send(400, "text/plain", "Failed to update Wi-Fi network");
      return;
    }

    LOG_DBG("WEB", "Updated Wi-Fi network at index %d (SSID: %s)", idx, ssid.c_str());
  } else {
    if (!WIFI_STORE.addCredential(ssid, password)) {
      server->send(400, "text/plain", "Cannot add network (limit reached)");
      return;
    }
    LOG_DBG("WEB", "Added Wi-Fi network: %s", ssid.c_str());
  }

  server->send(200, "text/plain", "OK");
}

// Uses POST (not HTTP DELETE) because ESP32 WebServer doesn't support DELETE with body.
void CrossPointWebServer::handleDeleteWifiNetwork() {
  if (!server->hasArg("plain")) {
    server->send(400, "text/plain", "Missing JSON body");
    return;
  }

  const String body = server->arg("plain");
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, body);
  if (err) {
    server->send(400, "text/plain", String("Invalid JSON: ") + err.c_str());
    return;
  }

  if (!doc["index"].is<int>()) {
    server->send(400, "text/plain", "Missing index");
    return;
  }

  int idx = doc["index"].as<int>();
  if (idx < 0) {
    server->send(400, "text/plain", "Invalid network index");
    return;
  }
  const auto ssid = WIFI_STORE.getSsidAt(static_cast<size_t>(idx));
  if (!ssid) {
    server->send(400, "text/plain", "Invalid network index");
    return;
  }

  if (!WIFI_STORE.removeCredential(*ssid)) {
    server->send(400, "text/plain", "Failed to delete Wi-Fi network");
    return;
  }

  LOG_DBG("WEB", "Deleted Wi-Fi network at index %d (SSID: %s)", idx, ssid->c_str());
  server->send(200, "text/plain", "OK");
}

// WebSocket callback trampoline
void CrossPointWebServer::wsEventCallback(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
  if (wsInstance) {
    wsInstance->onWebSocketEvent(num, type, payload, length);
  }
}

// WebSocket event handler for fast binary uploads
// Protocol:
//   1. Client sends TEXT message: "START:<filename>:<size>:<path>"
//   2. Client sends BINARY messages with file data chunks
//   3. Server sends TEXT "PROGRESS:<received>:<total>" after each chunk
//   4. Server sends TEXT "DONE" or "ERROR:<message>" when complete
void CrossPointWebServer::onWebSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_DISCONNECTED:
      LOG_DBG("WS", "Client %u disconnected", num);
      // Only clean up if this is the client that owns the active upload.
      // A new client may have already started a fresh upload before this
      // DISCONNECTED event fires (race condition on quick cancel + retry).
      if (num == wsUploadClientNum && wsUploadInProgress && wsUploadFile) {
        abortWsUpload("WS");
      }
      break;

    case WStype_CONNECTED: {
      LOG_DBG("WS", "Client %u connected", num);
      break;
    }

    case WStype_TEXT: {
      // Parse control messages
      String msg = String((char*)payload);
      LOG_DBG("WS", "Text from client %u: %s", num, msg.c_str());

      if (msg.startsWith("START:")) {
        // Reject any START while an upload is already active to prevent
        // leaking the open wsUploadFile handle (owning client re-START included)
        if (wsUploadInProgress) {
          wsServer->sendTXT(num, "ERROR:Upload already in progress");
          break;
        }

        // Parse: START:<filename>:<size>:<path>
        int firstColon = msg.indexOf(':', 6);
        int secondColon = msg.indexOf(':', firstColon + 1);

        if (firstColon > 0 && secondColon > 0) {
          wsUploadFileName = msg.substring(6, firstColon);
          if (!FsHelpers::isSafePathComponent(wsUploadFileName)) {
            LOG_DBG("WS", "START rejected: invalid filename '%s'", wsUploadFileName.c_str());
            wsServer->sendTXT(num, "ERROR:Invalid file name");
            return;
          }
          String sizeToken = msg.substring(firstColon + 1, secondColon);
          bool sizeValid = sizeToken.length() > 0;
          int digitStart = (sizeValid && sizeToken[0] == '+') ? 1 : 0;
          if (digitStart > 0 && sizeToken.length() < 2) sizeValid = false;
          for (int i = digitStart; i < (int)sizeToken.length() && sizeValid; i++) {
            if (!isdigit((unsigned char)sizeToken[i])) sizeValid = false;
          }
          if (!sizeValid) {
            LOG_DBG("WS", "START rejected: invalid size token '%s'", sizeToken.c_str());
            wsServer->sendTXT(num, "ERROR:Invalid START format");
            return;
          }
          wsUploadSize = sizeToken.toInt();
          wsUploadPath = normalizeWebPath(msg.substring(secondColon + 1));
          wsUploadReceived = 0;
          wsLastProgressSent = 0;
          wsUploadStartTime = millis();

          String filePath = wsUploadPath;
          if (!filePath.endsWith("/")) filePath += "/";
          filePath += wsUploadFileName;

          resetTaskWatchdogIfSubscribed();
          if (Storage.exists(filePath.c_str())) {
            LOG_DBG("WS", "Upload collision: %s", filePath.c_str());
            wsServer->sendTXT(num, "ERROR:File already exists: " + wsUploadFileName);
            return;
          }

          LOG_DBG("WS", "Starting upload: %s (%d bytes) to %s", wsUploadFileName.c_str(), wsUploadSize,
                  filePath.c_str());

          // Open file for writing
          resetTaskWatchdogIfSubscribed();
          if (!Storage.openFileForWrite("WS", filePath, wsUploadFile)) {
            wsServer->sendTXT(num, "ERROR:Failed to create file");
            wsUploadInProgress = false;
            wsUploadClientNum = 255;
            return;
          }
          resetTaskWatchdogIfSubscribed();

          // Zero-byte upload: complete immediately without waiting for BIN frames
          if (wsUploadSize == 0) {
            // Explicit close() required: file-scope global persists beyond function scope
            wsUploadFile.close();
            wsLastCompleteName = wsUploadFileName;
            wsLastCompleteSize = 0;
            wsLastCompleteAt = millis();
            LOG_DBG("WS", "Zero-byte upload complete: %s", filePath.c_str());
            clearBookCache(filePath.c_str());
            if (isLibraryBookFile(wsUploadFileName)) library::markLibraryIndexDirty();
            wsServer->sendTXT(num, "DONE");
            wsLastProgressSent = 0;
            break;
          }

          wsUploadClientNum = num;
          wsUploadInProgress = true;
          wsServer->sendTXT(num, "READY");
        } else {
          wsServer->sendTXT(num, "ERROR:Invalid START format");
        }
      }
      break;
    }

    case WStype_BIN: {
      if (!wsUploadInProgress || !wsUploadFile || num != wsUploadClientNum) {
        wsServer->sendTXT(num, "ERROR:No upload in progress");
        return;
      }

      // Write binary data directly to file
      size_t remaining = wsUploadSize - wsUploadReceived;
      if (length > remaining) {
        abortWsUpload("WS");
        wsServer->sendTXT(num, "ERROR:Upload overflow");
        return;
      }
      resetTaskWatchdogIfSubscribed();
      size_t written = wsUploadFile.write(payload, length);
      resetTaskWatchdogIfSubscribed();

      if (written != length) {
        abortWsUpload("WS");
        wsServer->sendTXT(num, "ERROR:Write failed - disk full?");
        return;
      }

      wsUploadReceived += written;

      // Send progress update (every 64KB or at end)
      if (wsUploadReceived - wsLastProgressSent >= 65536 || wsUploadReceived >= wsUploadSize) {
        String progress = "PROGRESS:" + String(wsUploadReceived) + ":" + String(wsUploadSize);
        wsServer->sendTXT(num, progress);
        wsLastProgressSent = wsUploadReceived;
      }

      // Check if upload complete
      if (wsUploadReceived >= wsUploadSize) {
        // Explicit close() required: file-scope global persists beyond function scope
        wsUploadFile.close();
        wsUploadInProgress = false;
        wsUploadClientNum = 255;

        wsLastCompleteName = wsUploadFileName;
        wsLastCompleteSize = wsUploadSize;
        wsLastCompleteAt = millis();

        unsigned long elapsed = millis() - wsUploadStartTime;
        float kbps = (elapsed > 0) ? (wsUploadSize / 1024.0) / (elapsed / 1000.0) : 0;

        LOG_DBG("WS", "Upload complete: %s (%d bytes in %lu ms, %.1f KB/s)", wsUploadFileName.c_str(), wsUploadSize,
                elapsed, kbps);

        // Clear epub cache after uploading the file
        String filePath = wsUploadPath;
        if (!filePath.endsWith("/")) filePath += "/";
        filePath += wsUploadFileName;
        clearBookCache(filePath.c_str());
        if (isLibraryBookFile(wsUploadFileName)) library::markLibraryIndexDirty();

        wsServer->sendTXT(num, "DONE");
        wsLastProgressSent = 0;
      }
      break;
    }

    default:
      break;
  }
}

// --- Font management handlers ---

void CrossPointWebServer::handleFontsPage() const {
  PhonePage::sendStatic(*server, FontsPageHtml, sizeof(FontsPageHtml), FontsPageHtmlETag, "text/html");
  LOG_DBG("WEB", "Served fonts page");
}

void CrossPointWebServer::handleFontList() const {
  // Pick up any uploads/deletes that happened since the last reader load.
  const_cast<SdCardFontSystem&>(sdFontSystem).refreshIfDirty();
  const auto& families = sdFontSystem.registry().getFamilies();

  JsonDocument doc;
  JsonArray arr = doc["families"].to<JsonArray>();
  doc["maxFamilies"] = SdCardFontRegistry::MAX_SD_FAMILIES;

  for (const auto& family : families) {
    JsonObject fObj = arr.add<JsonObject>();
    fObj["name"] = family.name;

    JsonArray sizes = fObj["sizes"].to<JsonArray>();
    for (uint8_t s : family.availableSizes()) {
      sizes.add(s);
    }

    JsonArray files = fObj["files"].to<JsonArray>();
    for (const auto& file : family.files) {
      JsonObject fileObj = files.add<JsonObject>();
      // Extract filename from full path
      const char* name = strrchr(file.path.c_str(), '/');
      fileObj["name"] = name ? name + 1 : file.path.c_str();

      // Stat the file for size
      HalFile f;
      if (Storage.openFileForRead("WEB", file.path.c_str(), f)) {
        fileObj["size"] = static_cast<unsigned long>(f.size());
        f.close();
      } else {
        fileObj["size"] = 0;
      }
    }
  }

  String json;
  serializeJson(doc, json);
  server->send(200, "application/json", json);
}

void CrossPointWebServer::handleFontUploadData() {
  HTTPUpload& upload = server->upload();

  switch (upload.status) {
    case UPLOAD_FILE_START: {
      resetTaskWatchdogIfSubscribed();
      String family = server->arg("family");
      fontUpload.file = HalFile();
      fontUpload.familyName.clear();
      fontUpload.filePath.clear();
      fontUpload.valid = false;
      fontUpload.magicChecked = false;
      fontUpload.bytesWritten = 0;
      fontUpload.bufferPos = 0;

      if (!FontInstaller::isValidFamilyName(family.c_str())) {
        LOG_ERR("WEB", "Invalid font family name: %s", family.c_str());
        break;
      }

      String filename = upload.filename;
      filename.replace(' ', '_');
      // Validate filename: rejects path traversal (../, /, \) and enforces
      // a .cpfont basename of alphanumeric + hyphen + underscore. Without
      // this an attacker could supply "../../.crosspoint/settings.json" as
      // a "filename" and have it written outside the fonts directory.
      if (!FontInstaller::isValidCpfontFilename(filename.c_str())) {
        LOG_ERR("WEB", "Invalid font filename: %s", filename.c_str());
        break;
      }

      fontUpload.familyName = family.c_str();

      // Create a temporary FontInstaller for directory creation
      FontInstaller installer(sdFontSystem.registry());
      if (!installer.ensureFamilyDir(family.c_str())) {
        LOG_ERR("WEB", "Failed to create font family dir");
        break;
      }

      char path[128];
      FontInstaller::buildFontPath(family.c_str(), filename.c_str(), path, sizeof(path));
      fontUpload.filePath = path;

      if (!Storage.openFileForWrite("WEB", path, fontUpload.file)) {
        LOG_ERR("WEB", "Failed to open font file for write: %s", path);
        break;
      }

      fontUpload.valid = true;
      LOG_DBG("WEB", "Font upload started: %s -> %s", filename.c_str(), path);
      break;
    }

    case UPLOAD_FILE_WRITE: {
      if (!fontUpload.valid) break;
      resetTaskWatchdogIfSubscribed();

      // Validate magic bytes on first chunk only
      if (!fontUpload.magicChecked && upload.currentSize >= 8) {
        if (memcmp(upload.buf, "CPFONT\0\0", 8) != 0) {
          LOG_ERR("WEB", "Invalid .cpfont magic bytes");
          fontUpload.valid = false;
          break;
        }
        fontUpload.magicChecked = true;
      }

      // Buffer writes for efficiency
      size_t remaining = upload.currentSize;
      const uint8_t* src = upload.buf;
      while (remaining > 0) {
        size_t space = FontUploadState::BUFFER_SIZE - fontUpload.bufferPos;
        size_t chunk = (remaining < space) ? remaining : space;
        memcpy(fontUpload.buffer.data() + fontUpload.bufferPos, src, chunk);
        fontUpload.bufferPos += chunk;
        src += chunk;
        remaining -= chunk;

        if (fontUpload.bufferPos >= FontUploadState::BUFFER_SIZE) {
          fontUpload.file.write(fontUpload.buffer.data(), fontUpload.bufferPos);
          fontUpload.bytesWritten += fontUpload.bufferPos;
          fontUpload.bufferPos = 0;
          resetTaskWatchdogIfSubscribed();
        }
      }
      break;
    }

    case UPLOAD_FILE_END: {
      // Flush remaining buffer
      if (fontUpload.valid && fontUpload.bufferPos > 0) {
        fontUpload.file.write(fontUpload.buffer.data(), fontUpload.bufferPos);
        fontUpload.bytesWritten += fontUpload.bufferPos;
        fontUpload.bufferPos = 0;
      }
      if (fontUpload.file.isOpen()) {
        fontUpload.file.close();
      }

      if (!fontUpload.valid && !fontUpload.filePath.empty()) {
        Storage.remove(fontUpload.filePath.c_str());
      }

      LOG_DBG("WEB", "Font upload end: valid=%d, %zu bytes", fontUpload.valid, fontUpload.bytesWritten);
      break;
    }

    case UPLOAD_FILE_ABORTED: {
      if (fontUpload.file) {
        fontUpload.file.close();
      }
      if (!fontUpload.filePath.empty()) {
        Storage.remove(fontUpload.filePath.c_str());
      }
      fontUpload.valid = false;
      LOG_DBG("WEB", "Font upload aborted");
      break;
    }
  }
}

void CrossPointWebServer::handleFontUpload() {
  if (fontUpload.valid) {
    sdFontSystem.markRegistryDirty();
    server->send(200, "application/json", "{\"ok\":true}");
    LOG_DBG("WEB", "Font upload complete: %s", fontUpload.filePath.c_str());
  } else {
    server->send(400, "application/json", "{\"error\":\"Invalid .cpfont file\"}");
  }
}

void CrossPointWebServer::handleFontDelete() {
  String body = server->arg("plain");
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body);

  if (err || !doc["family"].is<const char*>()) {
    server->send(400, "application/json", "{\"error\":\"Invalid request\"}");
    return;
  }

  const char* familyName = doc["family"];
  FontInstaller installer(sdFontSystem.registry());
  auto result = installer.deleteFamily(familyName);

  if (result == FontInstaller::Error::OK) {
    sdFontSystem.markRegistryDirty();
    server->send(200, "application/json", "{\"ok\":true}");
    LOG_DBG("WEB", "Deleted font family: %s", familyName);
  } else {
    server->send(500, "application/json", "{\"error\":\"Delete failed\"}");
    LOG_ERR("WEB", "Failed to delete font family: %s", familyName);
  }
}
