#pragma once

#include <HalStorage.h>
#include <NetworkUdp.h>
#include <WebServer.h>
#include <WebSocketsServer.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// Structure to hold file information
struct FileInfo {
  String name;
  size_t size;
  bool isEpub;
  bool isDirectory;
};

// Who may do what over HTTP, WebDAV and the upload WebSocket. The activity fills it in per session.
struct WebSecurityOptions {
  bool requireCode = true;     // access code gate on every route, WebDAV and WebSocket
  uint8_t accessLevel = 0;     // 0 Full, 1 Upload only (no delete/rename/move/settings/wifi/opds writes),
                               // 2 Read only (GET only; no uploads)
  bool allowFirmware = false;  // POST /api/firmware; false -> 403
  bool webdav = false;         // register WebDAVHandler only when true
  bool discovery = true;       // gates the UDP discovery listener (the mDNS name is the activity's)
};

// The least access a route needs; a session's access level admits routes up to its own.
enum class WebAccess : uint8_t { Read = 0, Upload = 1, Modify = 2 };

class CrossPointWebServer {
 public:
  struct WsUploadStatus {
    bool inProgress = false;
    size_t received = 0;
    size_t total = 0;
    std::string filename;
    std::string lastCompleteName;
    size_t lastCompleteSize = 0;
    unsigned long lastCompleteAt = 0;
  };

  // Used by POST upload handler
  struct UploadState {
    HalFile file;
    String fileName;
    String path = "/";
    size_t size = 0;
    bool success = false;
    String error = "";

    // Upload write buffer - batches small writes into larger SD card operations
    // 4KB is a good balance: large enough to reduce syscall overhead, small enough
    // to keep individual write times short and avoid watchdog issues
    static constexpr size_t UPLOAD_BUFFER_SIZE = 4096;  // 4KB buffer
    std::vector<uint8_t> buffer;
    size_t bufferPos = 0;

    UploadState() { buffer.resize(UPLOAD_BUFFER_SIZE); }
  } upload;

  CrossPointWebServer();
  ~CrossPointWebServer();

  // Security for this session; call before begin(). sessionToken is 32 hex characters (the `ems` cookie and the
  // `?t=` of the QR URL), typedCode the 6 digits shown on the device. Both are copied.
  void setSecurity(const WebSecurityOptions& options, const char* sessionToken, const char* typedCode);

  // millis() of the last authenticated request or WebSocket frame (idle auto-stop).
  uint32_t lastActivityMs() const { return lastActivity; }

  // Start the web server (call after WiFi is connected)
  void begin();

  // Stop the web server
  void stop();

  // Call this periodically to handle client requests
  void handleClient();

  // Check if server is running
  bool isRunning() const { return running; }

  WsUploadStatus getWsUploadStatus() const;

  // Get the port number
  uint16_t getPort() const { return port; }

  // Firmware pushed with POST /api/firmware waits here on the SD card until the reader confirms the install.
  static constexpr const char* FIRMWARE_STAGED_PATH = "/.crosspoint/ota/firmware.bin";

  // True once, right after a staged image finished uploading; `size` is its byte count.
  bool takeStagedFirmware(size_t& size);

 private:
  std::unique_ptr<WebServer> server = nullptr;
  std::unique_ptr<WebSocketsServer> wsServer = nullptr;
  bool running = false;
  bool apMode = false;  // true when running in AP mode, false for STA mode
  uint16_t port = 80;
  uint16_t wsPort = 81;  // WebSocket port
  NetworkUDP udp;
  bool udpActive = false;

  // Access control
  enum class RouteKind : uint8_t { Page, Api, Dav, Login };  // how an unauthenticated request is answered
  enum class Verdict : uint8_t { Ok, BadHost, BadOrigin, TooLarge, NeedLogin, SetCookie, Forbidden };
  WebSecurityOptions security;
  char sessionToken[33] = {0};
  char typedCode[7] = {0};
  uint32_t lastActivity = 0;
  uint8_t failedCodes = 0;
  uint32_t lockedUntil = 0;  // millis() deadline; 0 = not locked
  uint32_t wsLastFrame = 0;

  Verdict check(WebAccess needed, RouteKind kind, bool capBody);
  bool guard(WebAccess needed, RouteKind kind, bool capBody = false);
  void sendDenied(Verdict verdict, RouteKind kind);
  bool hostAccepted(const String& authority) const;
  bool tokenFromCookie() const;
  bool tokenMatches(const char* candidate, size_t length) const;
  bool basicAuthOk();
  bool tryCode(const char* candidate);
  bool locked();
  void handleLogin();
  void sendLoginPage();
  void route(const char* uri, HTTPMethod method, WebAccess level, RouteKind kind, const std::function<void()>& fn);
  void routeUpload(const char* uri, WebAccess level, const std::function<void()>& done,
                   const std::function<void()>& data);
  bool wsHandshakeHeader(const String& name, const String& value) const;
  static bool davGate(void* ctx, WebAccess needed, bool respond);

  // WebSocket upload state
  void onWebSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length);
  static void wsEventCallback(uint8_t num, WStype_t type, uint8_t* payload, size_t length);
  void abortWsUpload(const char* tag);

  // File scanning
  void scanFiles(const char* path, const std::function<void(FileInfo)>& callback) const;
  String formatFileSize(size_t bytes) const;
  bool isEpubFile(const String& filename) const;

  // Request handlers
  void handleRoot() const;
  void handleJszip() const;
  void handleAppCss() const;
  void handleAppJs() const;
  void handleNotFound() const;
  void handleStatus() const;
  void handleFileList() const;
  void handleFileListData() const;
  void handleDownload() const;
  void handleUpload(UploadState& state) const;
  void handleUploadPost(UploadState& state) const;
  void handleCreateFolder() const;
  void handleRename() const;
  void handleMove() const;
  void handleDelete() const;
  void handleGetBookMeta() const;
  void handlePostBookMeta();

  // Settings handlers
  void handleSettingsPage() const;
  void handleGetSettings() const;
  void handlePostSettings();

  // Dashboard handlers: layouts, widget settings, the to-do list, pictures for the Image tile
  void handleDashboardsPage() const;
  void handlePicturesPage() const;
  void handleGetDashboards() const;
  void handlePostDashboards();
  void handleGetTodo() const;
  void handlePostTodo();

  // Firmware push (POST /api/firmware, multipart field "firmware")
  void handleFirmwareUploadData();
  void handleFirmwareUploadDone();
  struct FirmwareUploadState {
    HalFile file;
    size_t size = 0;
    size_t limit = 0;  // OTA slot size
    size_t bufferPos = 0;
    bool started = false;
    bool ok = false;
    int httpCode = 400;
    const char* error = nullptr;
  } firmwareUpload;
  size_t stagedFirmwareSize = 0;  // 0 = nothing pending

  // Font management handlers
  void handleFontsPage() const;
  void handleFontList() const;
  void handleFontUpload();
  void handleFontUploadData();
  void handleFontDelete();

  // Dictionary management handlers
  void handleDictionariesPage() const;
  void handleDictionaryList() const;
  void handleDictionarySelect();
  void handleDictionaryDelete();

  // Font upload state
  struct FontUploadState {
    HalFile file;
    std::string familyName;
    std::string filePath;
    bool valid = false;
    bool magicChecked = false;
    size_t bytesWritten = 0;
    static constexpr size_t BUFFER_SIZE = 4096;
    std::vector<uint8_t> buffer;
    size_t bufferPos = 0;

    FontUploadState() { buffer.resize(BUFFER_SIZE); }
  } fontUpload;

  // OPDS server handlers
  void handleGetOpdsServers() const;
  void handlePostOpdsServer();
  void handleDeleteOpdsServer();

  // Wi-Fi credential handlers
  void handleGetWifiNetworks() const;
  void handlePostWifiNetwork();
  void handleDeleteWifiNetwork();
};
