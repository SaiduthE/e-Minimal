#pragma once

#include <GameSession.h>
#include <WebServer.h>

#include <memory>

// The HTTP half of game night: one page and its stylesheet for the phones, four
// endpoints, and the captive-portal redirect that makes "join the Wi-Fi" enough
// to be playing.
//
// Deliberately not CrossPointWebServer: that one carries WebDAV, WebSockets and
// the file manager, none of which belong in front of a room full of guests, and
// all of which cost heap that the game session and the AP need.
class GameServer {
 public:
  // Every view is serialized into this one buffer; test/party_games asserts the
  // largest document (Codenames, eight seats) fits with room to spare.
  static constexpr size_t JSON_BUFFER_SIZE = 3072;

  explicit GameServer(party::GameSession& session) : session_(session) {}
  ~GameServer() { stop(); }

  GameServer(const GameServer&) = delete;
  GameServer& operator=(const GameServer&) = delete;

  void begin();
  void stop();
  void handleClient();
  bool isRunning() const { return running_; }
  static constexpr uint16_t PORT = 80;

 private:
  void handlePlayPage();
  void handleStylesheet();
  void handleJoin();
  void handleState();
  void handleAction();
  void handleLeave();
  void handleNotFound();

  // Resolves ?t= to the caller's own seat, refreshing that phone's last-seen
  // stamp. Returns -1 for a browser that has not joined, which still gets the
  // spectator view.
  int8_t seatFromRequest();
  // The seat a request from `own` is served as: the test seat named in ?as=
  // (digits only) when test mode is on and `own` is the host, else `own`.
  int8_t viewSeat(int8_t own);
  // `seat` is the view served; `self` is the caller's own seat, sent as "me".
  void sendState(int8_t seat, int8_t self);

  party::GameSession& session_;
  std::unique_ptr<WebServer> server_;
  bool running_ = false;
  char json_[JSON_BUFFER_SIZE] = {};
};
