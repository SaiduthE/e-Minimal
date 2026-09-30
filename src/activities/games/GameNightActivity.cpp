#include "GameNightActivity.h"

#include <Arduino.h>
#include <FontCacheManager.h>
#include <I18n.h>
#include <WiFi.h>
#include <esp_random.h>

#include "GameBoards.h"
#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "components/PhoneJoinPanel.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/TaskWatchdog.h"

// Test players are a bench feature (platformio.ini); production builds leave
// them out.
#ifndef GAMES_TEST_MODE
#define GAMES_TEST_MODE 0
#endif

namespace {
constexpr const char* AP_SSID = "eMinimal Games";
constexpr const char* AP_HOSTNAME = "eminimal";
constexpr uint8_t AP_CHANNEL = 1;
// One seat per phone, so the access point has to hold the whole table.
constexpr uint8_t AP_MAX_CONNECTIONS = party::MAX_PLAYERS;
}  // namespace

void GameNightActivity::onEnter() {
  Activity::onEnter();
  session.setTestMode(GAMES_TEST_MODE != 0);
  LOG_DBG("GAMES", "Free heap at onEnter: %d bytes", ESP.getFreeHeap());

  // Same heap-critical transition as file transfer: WiFi plus the HTTP server
  // have to fit in what is left, and the SD font caches are rebuildable.
  if (auto* fontCache = renderer.getFontCacheManager()) {
    fontCache->releaseSdFontCaches();
  }

  session.reseed(esp_random());
  session.selectGame(initialGame);
  state = State::AP_STARTING;
  paintedVersion = 0;
  paintedAway = 0;
  paintsSinceFullRefresh = 0;
  requestUpdate();

  startAccessPoint();
}

void GameNightActivity::onExit() {
  Activity::onExit();
  state = State::SHUTTING_DOWN;

  server.reset();
  // Read before the portal drops the AP: that leaves the mode NULL too.
  const bool wifiWasOn = WiFi.getMode() != WIFI_MODE_NULL;
  portal.end();

  // Tearing WiFi down leaves the heap fragmented enough to hurt the reader, and
  // file transfer already answers that the same way. Switching apps to a book
  // lands back in that book.
  if (wifiWasOn) {
    if (activityManager.isEnteringReader()) {
      silentRestartToReader();
    } else {
      silentRestart();
    }
  }
  LOG_DBG("GAMES", "Free heap at onExit: %d bytes", ESP.getFreeHeap());
}

void GameNightActivity::startAccessPoint() {
  LOG_DBG("GAMES", "Starting game night access point");
  // Open, with the captive DNS that makes joining the network enough to land
  // on the game page.
  PhonePortal::Config config;
  config.ssid = AP_SSID;
  config.channel = AP_CHANNEL;
  config.maxClients = AP_MAX_CONNECTIONS;
  config.hostname = AP_HOSTNAME;
  if (!portal.begin(config)) {
    LOG_ERR("GAMES", "Failed to start the access point");
    activityManager.goToGames();
    return;
  }

  startServer();
}

void GameNightActivity::startServer() {
  server = std::make_unique<GameServer>(session);
  server->begin();
  if (!server->isRunning()) {
    LOG_ERR("GAMES", "Failed to start the game server");
    server.reset();
    activityManager.goToGames();
    return;
  }
  state = State::RUNNING;
  requestUpdate();
}

void GameNightActivity::hostAction(const party::Verb verb) {
  const int8_t host = session.hostSeat();
  if (host < 0) return;
  party::Action action;
  action.verb = verb;
  session.applyAction(static_cast<uint8_t>(host), action);
}

void GameNightActivity::openRoundMenu() {
  const char* const options[] = {tr(STR_GAMES_RESUME), tr(STR_GAMES_END_ROUND_ACTION), tr(STR_GAMES_LEAVE_NIGHT)};
  menuForRound = true;
  menu.show(games::titleFor(session.selected()), options, 3, 0, [this](const int index) { menuPick = index; });
  requestUpdate();
}

void GameNightActivity::openLeaveConfirm() {
  const char* const options[] = {tr(STR_GAMES_STAY), tr(STR_GAMES_LEAVE_NIGHT)};
  menuForRound = false;
  menu.show(tr(STR_GAMES_LEAVE_CONFIRM), options, 2, 0, [this](const int index) { menuPick = index; });
  requestUpdate();
}

bool GameNightActivity::applyMenuChoice() {
  const int pick = menuPick;
  menuPick = -1;
  // Round menu: Resume, End the round, Leave. Lobby: Stay, Leave.
  if (menuForRound && pick == 1) {
    session.endGame();
    return false;
  }
  return (menuForRound && pick == 2) || (!menuForRound && pick == 1);
}

bool GameNightActivity::handleInput() {
  if (mappedInput.wasHomeGesture()) {
    {
      RenderLock lock;
      menu.dismiss();
    }
    onGoHome();
    return true;
  }

  // Nothing pressed and no stale popup: skip the lock, which the render task
  // holds through a whole panel refresh.
  const bool staleMenu = menu.isActive() && !menuMatchesTable();
  if (!staleMenu && !mappedInput.wasAnyPressed() && !mappedInput.wasAnyReleased()) return false;

  bool leave = false;
  {
    // The session and the popup are what render() draws from.
    RenderLock lock;
    leave = applyInput(staleMenu);
  }
  if (leave) {
    // Back to the picker; onExit() reboots into it to shed the WiFi heap.
    // Switching activities takes the render lock, so it happens outside ours.
    activityManager.goToGames();
  }
  return leave;
}

bool GameNightActivity::applyInput(const bool staleMenu) {
  if (staleMenu) {
    // A round ended from a phone takes its menu with it, and one started from
    // a phone closes the lobby's leave question.
    menu.dismiss();
    requestUpdate();
    return false;
  }

  if (menu.isActive()) {
    // Phones keep playing underneath; the menu only takes the device's keys.
    menu.handleInput(mappedInput, [this] { requestUpdate(); });
    return applyMenuChoice();
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (!session.inLobby()) {
      // One stray Back must not throw a running round away.
      if (session.game()->isOver()) {
        session.endGame();
      } else {
        openRoundMenu();
      }
    } else if (session.playerCount() > 0) {
      openLeaveConfirm();
    } else {
      return true;
    }
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (session.inLobby()) {
      session.startGame();
    } else {
      hostAction(party::Verb::Next);
    }
  } else if (session.inLobby() && session.testMode()) {
    // Test players let one person open any game alone; the version bump they
    // cause is what repaints the seat list.
    if (mappedInput.wasReleased(MappedInputManager::Button::NavPrevious)) {
      session.addTestPlayer();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::NavNext)) {
      session.removeTestPlayer();
    }
  }
  return false;
}

void GameNightActivity::loop() {
  if (state != State::RUNNING || !server || !server->isRunning()) return;

  // Button events are one-shot and the request burst below pumps the input
  // itself, so read them before it runs and again inside it.
  if (handleInput()) return;

  portal.loop();

  // Same shape as the file-transfer loop: pump hard, reset the watchdog, and
  // keep reading buttons from inside the burst so the device stays responsive
  // while eight phones are polling.
  resetTaskWatchdogIfSubscribed();
  constexpr int MAX_ITERATIONS = 200;
  for (int i = 0; i < MAX_ITERATIONS && server->isRunning(); i++) {
    {
      // A request can deal, end or pack the table while render() reads it:
      // hold the render lock per request, so a render still fits between two.
      // Nothing under it waits on the render task.
      RenderLock lock;
      server->handleClient();
    }
    if ((i & 0x1F) == 0x1F) resetTaskWatchdogIfSubscribed();
    if ((i & 0x3F) == 0x3F) {
      yield();
      mappedInput.update();
      if (handleInput()) return;
    }
  }

  // Going away bumps no version (it is only time passing), so the lobby's seat
  // list watches the away set itself. The round's screen shows no away.
  uint8_t away = 0;
  if (session.inLobby()) {
    const uint32_t now = millis();
    for (uint8_t seat = 0; seat < party::MAX_PLAYERS; seat++) {
      if (session.isAway(seat, now)) away = static_cast<uint8_t>(away | (1u << seat));
    }
  }

  // One repaint per change, never faster than the panel can usefully follow.
  if ((session.version() != paintedVersion || away != paintedAway) &&
      millis() - lastPaintMs >= MIN_REPAINT_INTERVAL_MS) {
    paintedVersion = session.version();
    paintedAway = away;
    lastPaintMs = millis();
    requestUpdate();
  }
}

void GameNightActivity::renderLobby(const Rect& content) const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);

  // The join card every phone screen paints, in the upper three fifths so the
  // seat list underneath still has room on a landscape panel. Phones that
  // follow the captive portal never need its second code, and plenty do not.
  const std::string hostnameUrl = portal.hostnameUrl();
  const std::string altUrl = std::string(tr(STR_OR_HTTP_PREFIX)) + hostnameUrl.substr(sizeof("http://") - 1);
  PhoneJoinPanel::Content card;
  card.portal = &portal;
  card.pageUrl = portal.pageUrl();
  card.pageAlt = altUrl.c_str();
  // The card pads itself, so it spans the content plus its side padding.
  const int pad = metrics.contentSidePadding;
  const Rect cardBounds(content.x - pad, content.y, content.width + pad * 2, content.height * 3 / 5);
  const int cardBottom = PhoneJoinPanel::draw(renderer, cardBounds, card);

  // On a test build, one line at the bottom points a lone tester at the
  // test-player keys.
  int seatsBottom = content.y + content.height;
  if (session.testMode()) {
    const int helpLineHeight = renderer.getLineHeight(SMALL_FONT_ID);
    const int helpTop = seatsBottom - helpLineHeight;
    GUI.drawHelpText(renderer, Rect{content.x, helpTop, content.width, helpLineHeight}, tr(STR_GAMES_TEST_HINT));
    seatsBottom = helpTop - metrics.verticalSpacing;
  }

  const int listTop = cardBottom + metrics.verticalSpacing * 2;
  renderer.drawText(UI_10_FONT_ID, content.x, listTop, tr(STR_GAMES_PLAYERS), true, EpdFontFamily::BOLD);
  const int seatsTop = listTop + lineHeight + metrics.verticalSpacing;
  const Rect seats(content.x, seatsTop, content.width, seatsBottom - seatsTop);
  games::drawSeatList(renderer, session, seats, millis());
}

void GameNightActivity::renderRound(const Rect& content) const {
  Rect board = content;
  if (session.game() && session.game()->isOver()) {
    const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
    renderer.drawText(UI_10_FONT_ID, content.x, content.y + content.height - lineHeight, tr(STR_GAMES_ROUND_OVER), true,
                      EpdFontFamily::BOLD);
    board.height -= lineHeight + UITheme::scaledPx(4);
  }
  games::drawBoard(renderer, session, board);
}

void GameNightActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();

  if (state == State::AP_STARTING) {
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_GAME_NIGHT), nullptr);
    renderer.drawCenteredText(UI_10_FONT_ID, (pageHeight - renderer.getLineHeight(UI_10_FONT_ID)) / 2,
                              tr(STR_STARTING_HOTSPOT));
    renderer.displayBuffer();
    return;
  }

  const bool inLobby = session.inLobby();
  const bool roundOver = !inLobby && session.game()->isOver();
  char status[96];
  games::statusFor(session, status, sizeof(status));

  // The hints are drawn along the keys' edge, which is the bottom only in
  // portrait: the safe area keeps every orientation clear of them, and the
  // content stops one spacing short of that edge.
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  const int headerTop = safe.y + metrics.topPadding;
  GUI.drawHeader(renderer, Rect{safe.x, headerTop, safe.width, metrics.headerHeight},
                 inLobby ? tr(STR_GAME_NIGHT) : games::titleFor(session.selected()), nullptr);
  GUI.drawSubHeader(renderer, Rect{safe.x, headerTop + metrics.headerHeight, safe.width, metrics.tabBarHeight}, status);

  const int top = headerTop + metrics.headerHeight + metrics.tabBarHeight + metrics.verticalSpacing;
  const int bottom = safe.y + safe.height - metrics.verticalSpacing;
  const Rect content(safe.x + metrics.contentSidePadding, top, safe.width - metrics.contentSidePadding * 2,
                     bottom - top);

  if (inLobby) {
    renderLobby(content);
  } else {
    renderRound(content);
  }

  // The popup draws its own hints over the table and pushes the frame. One the
  // table has moved past (a round ended or started from a phone) is left out;
  // handleInput() dismisses it.
  if (menuMatchesTable() && menu.processRender(renderer, mappedInput)) return;

  // On a test build the lobby's previous/next keys seat and drop test players
  // (handleInput()). Mid-round Back opens the menu; after a round it is the
  // lobby.
  const bool testKeys = inLobby && session.testMode();
  const char* backLabel = inLobby ? tr(STR_BACK) : roundOver ? tr(STR_GAMES_END_ROUND) : tr(STR_GAMES_MENU);
  const char* confirmLabel = inLobby ? tr(STR_GAMES_START) : tr(STR_GAMES_NEXT);
  const char* addLabel = testKeys ? tr(STR_GAMES_ADD_TEST) : "";
  const char* dropLabel = testKeys ? tr(STR_GAMES_REMOVE_TEST) : "";
  const auto labels = mappedInput.mapLabels(backLabel, confirmLabel, addLabel, dropLabel);
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  // Fast refreshes keep the table moving; a full one every few paints clears the
  // ghosting they leave behind.
  if (++paintsSinceFullRefresh >= FULL_REFRESH_EVERY) {
    paintsSinceFullRefresh = 0;
    renderer.displayBuffer(HalDisplay::FULL_REFRESH);
  } else {
    renderer.displayBuffer();
  }
}
