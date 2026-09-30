#include "GameServer.h"

#include <Arduino.h>
#include <Logging.h>
#include <esp_random.h>

#include <cstdlib>

#include "PhonePage.h"
#include "html/PlayPageHtml.generated.h"
#include "html/css/appCss.generated.h"

namespace {

struct VerbName {
  const char* name;
  party::Verb verb;
};

constexpr VerbName VERBS[] = {
    {"ready", party::Verb::Ready},       {"next", party::Verb::Next}, {"clue", party::Verb::Clue},
    {"guess", party::Verb::Guess},       {"pass", party::Verb::Pass}, {"place", party::Verb::Place},
    {"fire", party::Verb::Fire},         {"vote", party::Verb::Vote}, {"roll", party::Verb::Roll},
    {"move", party::Verb::Move},         {"pick", party::Verb::Pick}, {"addtest", party::Verb::AddTest},
    {"droptest", party::Verb::DropTest}, {"end", party::Verb::End},   {"shape", party::Verb::Shape},
};

party::Verb parseVerb(const String& name) {
  for (const VerbName& entry : VERBS) {
    if (name == entry.name) return entry.verb;
  }
  return party::Verb::None;
}

int16_t parseArg(WebServer& server, const char* name) {
  const String value = server.arg(name);
  if (value.isEmpty()) return -1;
  return static_cast<int16_t>(value.toInt());
}

uint32_t newToken() {
  uint32_t token = esp_random();
  // 0 marks a free seat, so it can never be handed out as a token.
  while (token == 0) token = esp_random();
  return token;
}

}  // namespace

void GameServer::begin() {
  if (running_) return;

  server_.reset(new (std::nothrow) WebServer(PORT));
  if (!server_) {
    LOG_ERR("GAMESRV", "OOM: WebServer");
    return;
  }

  server_->on("/", HTTP_GET, [this] { handlePlayPage(); });
  server_->on("/css/app.css", HTTP_GET, [this] { handleStylesheet(); });
  server_->on("/api/game/state", HTTP_GET, [this] { handleState(); });
  server_->on("/api/game/state", HTTP_POST, [this] { handleState(); });
  server_->on("/api/game/join", HTTP_POST, [this] { handleJoin(); });
  server_->on("/api/game/act", HTTP_POST, [this] { handleAction(); });
  server_->on("/api/game/leave", HTTP_POST, [this] { handleLeave(); });
  server_->onNotFound([this] { handleNotFound(); });

  const char* collected[] = {"If-None-Match"};
  server_->collectHeaders(collected, 1);
  server_->begin();
  running_ = true;
  LOG_DBG("GAMESRV", "Game server listening on port %u, free heap %d", PORT, ESP.getFreeHeap());
}

void GameServer::stop() {
  if (!running_) return;
  running_ = false;
  if (server_) {
    server_->stop();
    server_.reset();
  }
  LOG_DBG("GAMESRV", "Game server stopped, free heap %d", ESP.getFreeHeap());
}

void GameServer::handleClient() {
  if (running_ && server_) server_->handleClient();
}

void GameServer::handlePlayPage() {
  // The page is immutable for the life of a firmware image, so a strong ETag
  // keeps a phone that reloads mid-game off the flash entirely.
  PhonePage::sendStatic(*server_, PlayPageHtml, PlayPageHtmlCompressedSize, PlayPageHtmlETag, "text/html");
}

void GameServer::handleStylesheet() {
  PhonePage::sendStatic(*server_, appCss, appCssCompressedSize, appCssETag, "text/css");
}

int8_t GameServer::seatFromRequest() {
  const String raw = server_->arg("t");
  if (raw.isEmpty()) return -1;
  const uint32_t token = strtoul(raw.c_str(), nullptr, 16);
  const int8_t seat = session_.seatForToken(token);
  if (seat >= 0) session_.touch(static_cast<uint8_t>(seat), millis());
  return seat;
}

int8_t GameServer::viewSeat(const int8_t own) {
  // A test seat's view carries its secrets, so only the host may play one, and
  // only on a test build.
  if (!session_.testMode() || own < 0 || !session_.isHost(static_cast<uint8_t>(own))) return own;
  const String raw = server_->arg("as");
  if (raw.isEmpty()) return own;
  unsigned target = 0;
  for (unsigned i = 0; i < raw.length(); i++) {
    const char c = raw[i];
    if (c < '0' || c > '9') return own;
    target = target * 10 + static_cast<unsigned>(c - '0');
    if (target >= party::MAX_PLAYERS) return own;
  }
  return session_.isTestSeat(static_cast<uint8_t>(target)) ? static_cast<int8_t>(target) : own;
}

void GameServer::sendState(const int8_t seat, const int8_t self) {
  const size_t written = session_.writeStateJson(json_, sizeof(json_), seat, self, millis());
  server_->sendHeader("Cache-Control", "no-store");
  server_->send_P(200, "application/json", json_, written);
}

void GameServer::handleState() {
  const int8_t own = seatFromRequest();
  sendState(viewSeat(own), own);
}

void GameServer::handleJoin() {
  const uint32_t token = newToken();
  const String name = server_->arg("name");
  const bool rejoin = server_->arg("rejoin") == "1";
  const uint8_t before = session_.playerCount();
  const int8_t seat = session_.join(name.c_str(), token, millis(), rejoin);
  server_->sendHeader("Cache-Control", "no-store");
  if (seat == party::GameSession::JOIN_NAME_TAKEN || seat == party::GameSession::JOIN_CAN_REJOIN) {
    // Answer with the seat's own spelling. Names are sanitized, so nothing in
    // them needs escaping in JSON.
    char clean[party::MAX_NAME_LEN + 1];
    party::sanitizeName(name.c_str(), clean, sizeof(clean));
    const int8_t match = session_.seatNamed(clean);
    const char* held = match >= 0 ? session_.player(static_cast<uint8_t>(match)).name : clean;
    char body[128];
    if (seat == party::GameSession::JOIN_CAN_REJOIN) {
      snprintf(body, sizeof(body), "{\"error\":\"%s is away - tap Rejoin if that is you\",\"rejoin\":\"%s\"}", held,
               held);
    } else {
      snprintf(body, sizeof(body), "{\"error\":\"%s is already at the table - pick another name\"}", held);
    }
    server_->send(200, "application/json", body);
    return;
  }
  if (seat < 0) {
    if (session_.inLobby()) {
      server_->send(200, "application/json", "{\"error\":\"No free seat right now\"}");
    } else {
      server_->send(200, "application/json", "{\"error\":\"A round is on - rejoin under your name, or watch\"}");
    }
    return;
  }

  // A fresh token either takes a new seat or, with `rejoin`, reclaims an away
  // one by name.
  const bool rejoined = session_.playerCount() == before;
  char body[72];
  snprintf(body, sizeof(body), "{\"t\":\"%08lx\",\"seat\":%d,\"rejoined\":%s}", static_cast<unsigned long>(token),
           seat, rejoined ? "true" : "false");
  server_->send(200, "application/json", body);
  LOG_DBG("GAMESRV", "Seat %d %s as %s", seat, rejoined ? "rejoined" : "joined",
          session_.player(static_cast<uint8_t>(seat)).name);
}

void GameServer::handleAction() {
  const int8_t own = seatFromRequest();
  if (own < 0) {
    server_->send(403, "application/json", "{\"error\":\"Join first\"}");
    return;
  }
  const int8_t seat = viewSeat(own);
  const uint32_t token = session_.player(static_cast<uint8_t>(own)).token;

  party::Action action;
  action.verb = parseVerb(server_->arg("v"));
  action.a = parseArg(*server_, "a");
  action.b = parseArg(*server_, "b");
  action.c = parseArg(*server_, "c");
  const String text = server_->arg("s");
  if (!text.isEmpty()) party::sanitizeName(text.c_str(), action.text, sizeof(action.text));

  session_.applyAction(static_cast<uint8_t>(seat), action);
  // Answer with the state the action produced: one round trip instead of
  // waiting out the phone's poll interval. Starting a round packs the seats
  // down, and only the host playing its own seat can start one, so the token
  // finds the caller again.
  const int8_t self = session_.seatForToken(token);
  sendState(seat == own ? self : seat, self);
}

void GameServer::handleLeave() {
  // Always the caller's own seat: a test seat is dropped from the lobby instead.
  const int8_t seat = seatFromRequest();
  if (seat >= 0 && !session_.leave(static_cast<uint8_t>(seat))) {
    // Mid-round the seat holds a role: it stays, and so does the caller's view.
    sendState(seat, seat);
    return;
  }
  sendState(-1, -1);
}

void GameServer::handleNotFound() {
  if (server_->method() == HTTP_OPTIONS) {
    server_->send(204, "text/plain", "");
    return;
  }
  // Everything that is not an API call goes to the page. Phones probe a handful
  // of well-known URLs to detect a captive portal; answering them with a
  // redirect is what pops the browser open by itself.
  if (!server_->uri().startsWith("/api/")) {
    server_->sendHeader("Location", "/", true);
    server_->send(302, "text/plain", "");
    return;
  }
  server_->send(404, "application/json", "{\"error\":\"Unknown endpoint\"}");
}
