#include "GameSession.h"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <new>

namespace party {

namespace {

constexpr GameMeta kGames[GAME_COUNT] = {
    {GameId::Undercover, "undercover", "Undercover", 3, MAX_PLAYERS},
    {GameId::Codenames, "codenames", "Codenames", 4, MAX_PLAYERS},
    {GameId::Battleship, "battleship", "Battleship", 2, 2},
    {GameId::Ludo, "ludo", "Ludo", 2, 4},
};

constexpr GameMeta kNoGame = {GameId::None, "none", "None", 0, 0};

// Names are sanitized printable ASCII, so an ASCII fold is enough.
bool sameName(const char* a, const char* b) {
  for (; *a && *b; a++, b++) {
    if (std::tolower(static_cast<unsigned char>(*a)) != std::tolower(static_cast<unsigned char>(*b))) return false;
  }
  return *a == *b;
}

}  // namespace

const GameMeta& gameMeta(const GameId id) {
  for (const GameMeta& meta : kGames) {
    if (meta.id == id) return meta;
  }
  return kNoGame;
}

GameId gameAt(const uint8_t index) { return index < GAME_COUNT ? kGames[index].id : GameId::None; }

namespace {
// The in-place storage has to cover every game; bump GAME_STORAGE_BYTES if a new
// one outgrows it.
template <typename T>
constexpr bool fitsInStorage() {
  return sizeof(T) <= GameSession::GAME_STORAGE_BYTES && alignof(T) <= 8;
}
static_assert(fitsInStorage<Undercover>(), "Undercover outgrew GameSession storage");
static_assert(fitsInStorage<Codenames>(), "Codenames outgrew GameSession storage");
static_assert(fitsInStorage<Battleship>(), "Battleship outgrew GameSession storage");
static_assert(fitsInStorage<Ludo>(), "Ludo outgrew GameSession storage");
}  // namespace

uint8_t gameIndex(const GameId id) {
  for (uint8_t i = 0; i < GAME_COUNT; i++) {
    if (kGames[i].id == id) return i;
  }
  return 0;
}

int8_t GameSession::join(const char* name, const uint32_t token, const uint32_t nowMs, const bool rejoin) {
  if (token == 0) return -1;

  const int8_t existing = seatForToken(token);
  if (existing >= 0) {
    touch(static_cast<uint8_t>(existing), nowMs);
    return existing;
  }

  // A phone that lost its token (a closed portal sheet, a browser without the
  // first one's storage) comes back under its name, even mid-round, keeping the
  // seat's role - but only when it asks to, so a namesake cannot take the seat
  // by accident. A seat still polling, or a test seat, belongs to someone else.
  char clean[MAX_NAME_LEN + 1];
  const bool named = sanitizeName(name, clean, sizeof(clean)) > 0;
  const int8_t match = named ? seatNamed(clean) : -1;
  if (match >= 0) {
    Player& player = players_[match];
    if (player.test || !isAway(static_cast<uint8_t>(match), nowMs)) return JOIN_NAME_TAKEN;
    if (!rejoin) return JOIN_CAN_REJOIN;
    player.token = token;
    player.lastSeenMs = nowMs;
    bump();
    return match;
  }

  // Seats only open in the lobby: a round in progress has already dealt roles.
  if (game_ != nullptr) return -1;

  for (uint8_t seat = 0; seat < MAX_PLAYERS; seat++) {
    if (players_[seat].seated) continue;
    Player& player = players_[seat];
    if (named) {
      memcpy(player.name, clean, sizeof(player.name));
    } else {
      freeName("Player", seat, player.name, sizeof(player.name));
    }
    player.token = token;
    player.lastSeenMs = nowMs;
    player.seated = true;
    count_++;
    // A phone takes the host's controls over from a test seat.
    if (count_ == 1 || players_[hostSeat_].test) hostSeat_ = seat;
    bump();
    return static_cast<int8_t>(seat);
  }
  return -1;
}

int8_t GameSession::addTestPlayer() {
  // Seats only open in the lobby: a round in progress has already dealt roles.
  if (!testMode_ || game_ != nullptr) return -1;

  for (uint8_t seat = 0; seat < MAX_PLAYERS; seat++) {
    if (players_[seat].seated) continue;
    // Never handed to a phone; it only has to be nonzero and unique so the seat
    // reads as taken.
    uint32_t token = 0;
    bool taken = true;
    while (token == 0 || taken) {
      token = rng_.next();
      taken = false;
      for (const Player& other : players_) {
        if (other.seated && other.token == token) taken = true;
      }
    }
    Player& player = players_[seat];
    freeName("Test", seat, player.name, sizeof(player.name));
    player.token = token;
    player.lastSeenMs = 0;
    player.seated = true;
    player.test = true;
    count_++;
    if (count_ == 1) hostSeat_ = seat;
    bump();
    return static_cast<int8_t>(seat);
  }
  return -1;
}

bool GameSession::removeTestPlayer() {
  if (!testMode_ || game_ != nullptr) return false;
  for (uint8_t seat = MAX_PLAYERS; seat-- > 0;) {
    if (isTestSeat(seat)) return leave(seat);
  }
  return false;
}

bool GameSession::isTestSeat(const uint8_t seat) const {
  return seat < MAX_PLAYERS && players_[seat].seated && players_[seat].test;
}

uint8_t GameSession::testPlayerCount() const {
  uint8_t count = 0;
  for (uint8_t seat = 0; seat < MAX_PLAYERS; seat++) {
    if (isTestSeat(seat)) count++;
  }
  return count;
}

int8_t GameSession::seatForToken(const uint32_t token) const {
  if (token == 0) return -1;
  for (uint8_t seat = 0; seat < MAX_PLAYERS; seat++) {
    // A test seat has no phone behind it, so no request resolves to it.
    if (players_[seat].seated && !players_[seat].test && players_[seat].token == token) {
      return static_cast<int8_t>(seat);
    }
  }
  return -1;
}

int8_t GameSession::seatNamed(const char* name) const {
  if (!name || !name[0]) return -1;
  for (uint8_t seat = 0; seat < MAX_PLAYERS; seat++) {
    if (players_[seat].seated && sameName(players_[seat].name, name)) return static_cast<int8_t>(seat);
  }
  return -1;
}

void GameSession::freeName(const char* prefix, const uint8_t seat, char* out, const size_t cap) const {
  // "<prefix> <seat + 1>", unless someone already goes by it (packing moves
  // players off their seat numbers, and the phone page tells seats apart by
  // name): then the lowest free number. Seven others can hold at most seven.
  snprintf(out, cap, "%s %u", prefix, seat + 1u);
  for (unsigned number = 1; seatNamed(out) >= 0 && number <= MAX_PLAYERS; number++) {
    snprintf(out, cap, "%s %u", prefix, number);
  }
}

void GameSession::touch(const uint8_t seat, const uint32_t nowMs) {
  if (seat >= MAX_PLAYERS || !players_[seat].seated) return;
  // Only the lobby's seat list shows away, so only there is a return a change.
  if (game_ == nullptr && isAway(seat, nowMs)) bump();
  players_[seat].lastSeenMs = nowMs;
}

bool GameSession::leave(const uint8_t seat) {
  if (game_ != nullptr || seat >= MAX_PLAYERS || !players_[seat].seated) return false;
  players_[seat] = Player{};
  count_--;
  rehost();
  bump();
  return true;
}

void GameSession::rehost() {
  const Player& current = players_[hostSeat_];
  if (current.seated && !current.test) return;
  int8_t firstTest = -1;
  for (uint8_t seat = 0; seat < MAX_PLAYERS; seat++) {
    if (!players_[seat].seated) continue;
    if (!players_[seat].test) {
      hostSeat_ = seat;
      return;
    }
    if (firstTest < 0) firstTest = static_cast<int8_t>(seat);
  }
  // No phone at the table: a seated test host keeps it, else the first test seat.
  if (current.seated) return;
  hostSeat_ = firstTest >= 0 ? static_cast<uint8_t>(firstTest) : 0;
}

const Player& GameSession::player(const uint8_t seat) const {
  static const Player empty{};
  return seat < MAX_PLAYERS ? players_[seat] : empty;
}

bool GameSession::isAway(const uint8_t seat, const uint32_t nowMs) const {
  // A test seat never polls; whoever plays it is on their own seat's phone.
  if (seat >= MAX_PLAYERS || !players_[seat].seated || players_[seat].test) return false;
  return nowMs - players_[seat].lastSeenMs > AWAY_AFTER_MS;
}

void GameSession::selectGame(const GameId game) {
  if (game == GameId::None || game == selected_) return;
  selected_ = game;
  bump();
}

void GameSession::cycleGame(const int8_t delta) {
  const int8_t current = static_cast<int8_t>(gameIndex(selected_));
  int8_t next = static_cast<int8_t>((current + delta) % static_cast<int8_t>(GAME_COUNT));
  if (next < 0) next = static_cast<int8_t>(next + GAME_COUNT);
  selectGame(gameAt(static_cast<uint8_t>(next)));
}

bool GameSession::canStart() const {
  // maxPlayers is how many seats the game actually deals in; the rest watch the
  // shared screen, so only the minimum gates the start.
  return game_ == nullptr && count_ >= gameMeta(selected_).minPlayers;
}

bool GameSession::startGame() {
  if (!canStart()) return false;

  switch (selected_) {
    case GameId::Undercover:
      game_ = new (storage_) Undercover();
      break;
    case GameId::Codenames:
      game_ = new (storage_) Codenames();
      break;
    case GameId::Battleship:
      game_ = new (storage_) Battleship();
      break;
    case GameId::Ludo:
      game_ = new (storage_) Ludo();
      break;
    default:
      return false;
  }
  packSeats();
  game_->start(count_, rng_);
  if (selected_ == GameId::Ludo) {
    // Ludo's messages name who took which shape.
    auto* ludo = static_cast<Ludo*>(game_);
    for (uint8_t seat = 0; seat < count_ && seat < 4; seat++) ludo->setPlayerName(seat, players_[seat].name);
  }
  bump();
  return true;
}

void GameSession::packSeats() {
  // A seat freed in the lobby would otherwise be dealt in while the player
  // above it sat out. Only moves down, so the target is always free.
  uint8_t next = 0;
  for (uint8_t seat = 0; seat < MAX_PLAYERS; seat++) {
    if (!players_[seat].seated) continue;
    if (seat != next) {
      players_[next] = players_[seat];
      players_[seat] = Player{};
      if (hostSeat_ == seat) hostSeat_ = next;
    }
    next++;
  }
}

void GameSession::destroyGame() {
  if (!game_) return;
  game_->~Game();
  game_ = nullptr;
}

void GameSession::endGame() {
  if (!game_) return;
  destroyGame();
  bump();
}

bool GameSession::applyAction(const uint8_t seat, const Action& action) {
  if (seat >= MAX_PLAYERS || !players_[seat].seated) return false;

  // The host can call a round off whether it is running or over.
  if (action.verb == Verb::End) {
    if (!game_ || !isHost(seat)) return false;
    endGame();
    return true;
  }

  if (game_ && !game_->isOver()) {
    if (game_->apply(seat, action, isHost(seat))) {
      bump();
      return true;
    }
    return false;
  }

  // Lobby, or a finished round: only the host drives.
  if (!isHost(seat)) return false;

  switch (action.verb) {
    case Verb::Pick: {
      const GameId picked = gameAt(static_cast<uint8_t>(action.a));
      if (picked == GameId::None) return false;
      destroyGame();
      selected_ = picked;
      bump();
      return true;
    }
    case Verb::Next: {
      if (game_) {
        // Deal the same game again.
        destroyGame();
      }
      return startGame();
    }
    case Verb::Ready: {
      if (!game_) return false;
      endGame();
      return true;
    }
    case Verb::AddTest:
      return addTestPlayer() >= 0;
    case Verb::DropTest:
      return removeTestPlayer();
    default:
      return false;
  }
}

void GameSession::statusLine(char* out, const size_t cap) const {
  if (!out || cap == 0) return;
  if (game_) {
    game_->statusLine(out, cap);
    return;
  }
  const GameMeta& meta = gameMeta(selected_);
  if (count_ < meta.minPlayers) {
    snprintf(out, cap, "%s needs %u players - %u joined", meta.title, meta.minPlayers, count_);
  } else {
    snprintf(out, cap, "%s - %u joined, ready to start", meta.title, count_);
  }
}

size_t GameSession::writeStateJson(char* out, const size_t cap, const int8_t seat, const int8_t self,
                                   const uint32_t nowMs) const {
  JsonBuf json(out, cap);
  const GameMeta& meta = gameMeta(selected_);
  const bool seated = seat >= 0 && seat < static_cast<int8_t>(MAX_PLAYERS) && players_[seat].seated;
  const bool selfSeated = self >= 0 && self < static_cast<int8_t>(MAX_PLAYERS) && players_[self].seated;

  json.ch('{');
  json.keyNum("v", static_cast<long>(version_));
  json.ch(',');
  json.keyNum("seat", seated ? seat : -1);
  json.ch(',');
  json.keyNum("me", selfSeated ? self : -1);
  json.ch(',');
  json.keyStr("name", seated ? players_[seat].name : "");
  json.ch(',');
  json.keyBool("host", seated && isHost(static_cast<uint8_t>(seat)));
  json.ch(',');
  json.keyStr("game", meta.key);
  json.ch(',');
  json.keyStr("title", meta.title);
  json.ch(',');
  json.keyStr("phase", game_ ? "play" : "lobby");
  json.ch(',');
  json.keyBool("canStart", canStart());
  json.ch(',');
  json.keyBool("testMode", testMode_);
  json.ch(',');
  json.keyNum("minPlayers", meta.minPlayers);
  json.ch(',');
  json.keyNum("maxPlayers", meta.maxPlayers);
  json.ch(',');

  char status[96];
  statusLine(status, sizeof(status));
  json.keyStr("status", status);
  json.ch(',');

  json.key("players");
  json.ch('[');
  bool first = true;
  for (uint8_t i = 0; i < MAX_PLAYERS; i++) {
    if (!players_[i].seated) continue;
    if (!first) json.ch(',');
    first = false;
    json.ch('{');
    json.keyNum("s", i);
    json.ch(',');
    json.keyStr("n", players_[i].name);
    json.ch(',');
    json.keyBool("host", i == hostSeat_);
    json.ch(',');
    json.keyBool("away", isAway(i, nowMs));
    json.ch(',');
    json.keyBool("test", players_[i].test);
    json.ch('}');
  }
  json.ch(']');

  if (!game_) {
    json.ch(',');
    json.key("games");
    json.ch('[');
    for (uint8_t i = 0; i < GAME_COUNT; i++) {
      const GameMeta& entry = gameMeta(gameAt(i));
      if (i) json.ch(',');
      json.ch('{');
      json.keyNum("i", i);
      json.ch(',');
      json.keyStr("k", entry.key);
      json.ch(',');
      json.keyStr("t", entry.title);
      json.ch(',');
      json.keyNum("min", entry.minPlayers);
      json.ch(',');
      json.keyNum("max", entry.maxPlayers);
      json.ch('}');
    }
    json.ch(']');
  } else {
    json.ch(',');
    json.keyBool("over", game_->isOver());
    json.ch(',');
    json.key("g");
    game_->writeView(json, seated ? seat : -1);
  }

  json.ch('}');
  return json.size();
}

}  // namespace party
