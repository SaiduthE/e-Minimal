#pragma once

#include "Battleship.h"
#include "Codenames.h"
#include "Game.h"
#include "Ludo.h"
#include "Undercover.h"

namespace party {

// The lobby and the round in progress: seats, join tokens, which game is
// selected, and the JSON document each phone polls.
//
// The active game is constructed in place in `storage_` rather than on the heap.
// A party session churns through rounds, and a repeated new/delete of a ~500
// byte object is exactly the fragmentation the reader cannot afford once WiFi
// and the HTTP server have taken their share of DRAM.
class GameSession {
 public:
  // A phone that has not polled within this window shows as away in the lobby.
  // The seat is kept: someone whose screen locked is still playing, and a phone
  // that lost its token can rejoin it by name.
  static constexpr uint32_t AWAY_AFTER_MS = 10000;
  // join(): the name belongs to a test seat or to a phone that is still polling.
  static constexpr int8_t JOIN_NAME_TAKEN = -2;
  // join(): the name's phone seat is away; asking again with `rejoin` takes it.
  static constexpr int8_t JOIN_CAN_REJOIN = -3;

  // Sized to the largest game; Battleship's two 10x10 grids dominate.
  static constexpr size_t GAME_STORAGE_BYTES = 640;

  explicit GameSession(uint32_t seed = 0x9E3779B9u) : rng_(seed) {}
  ~GameSession() { destroyGame(); }

  GameSession(const GameSession&) = delete;
  GameSession& operator=(const GameSession&) = delete;

  // Lobby -------------------------------------------------------------------
  // Returns the seat index. A token already in use returns its existing seat,
  // so a phone reload does not consume a seat. A name already at the table
  // (case-insensitive) never takes a second seat: an away phone seat answers
  // JOIN_CAN_REJOIN, and with `rejoin` is reclaimed under the new token, in the
  // lobby or mid-round; any other match is JOIN_NAME_TAKEN. A new name gets -1
  // while a round runs or the table is full. The first phone to join takes the
  // host over from a test seat.
  int8_t join(const char* name, uint32_t token, uint32_t nowMs, bool rejoin = false);
  int8_t seatForToken(uint32_t token) const;
  // The seat whose player carries `name` (case-insensitive), or -1.
  int8_t seatNamed(const char* name) const;
  // Bumps the version for an away phone coming back, in the lobby only: nothing
  // on the round's screen shows away.
  void touch(uint8_t seat, uint32_t nowMs);
  // Lobby only: a round has dealt the seat a role.
  bool leave(uint8_t seat);
  uint8_t playerCount() const { return count_; }
  const Player& player(uint8_t seat) const;
  bool isSeated(uint8_t seat) const { return seat < MAX_PLAYERS && players_[seat].seated; }
  bool isHost(uint8_t seat) const { return count_ > 0 && seat == hostSeat_; }
  int8_t hostSeat() const { return count_ > 0 ? static_cast<int8_t>(hostSeat_) : -1; }
  bool isAway(uint8_t seat, uint32_t nowMs) const;

  // Test players: seats with no phone behind them, so one person can open a
  // game alone and play every seat from one phone (GameServer's `as`). A bench
  // feature, off unless the device turns it on (GAMES_TEST_MODE).
  void setTestMode(bool on) { testMode_ = on; }
  bool testMode() const { return testMode_; }
  // Lobby only. Returns the seat, or -1 when the table is full, a round runs or
  // test mode is off.
  int8_t addTestPlayer();
  // Lobby only: drops the highest-numbered test player. False when there is none
  // or test mode is off.
  bool removeTestPlayer();
  bool isTestSeat(uint8_t seat) const;
  uint8_t testPlayerCount() const;

  // Game selection ----------------------------------------------------------
  GameId selected() const { return selected_; }
  void selectGame(GameId game);
  void cycleGame(int8_t delta);
  bool canStart() const;
  // Packs the seated players down to seats 0..N-1 (games deal the first N
  // seats), then deals. Tokens move with their players.
  bool startGame();
  void endGame();
  bool inLobby() const { return game_ == nullptr; }
  Game* game() { return game_; }
  const Game* game() const { return game_; }

  // Play --------------------------------------------------------------------
  // Routes an action to the round, or handles the lobby and post-game verbs the
  // host drives; End is the host's from any point of a round. Returns true when
  // something changed.
  bool applyAction(uint8_t seat, const Action& action);
  uint32_t version() const { return version_; }
  void bump() { version_++; }
  void reseed(uint32_t seed) { rng_.reseed(seed); }

  // Views -------------------------------------------------------------------
  // The document a phone polls, from the point of view of `seat` (-1 for a
  // browser that has not joined). `self` is the caller's own seat, sent as "me":
  // it differs from `seat` while the host plays a test seat. Returns the number
  // of bytes written.
  size_t writeStateJson(char* out, size_t cap, int8_t seat, int8_t self, uint32_t nowMs) const;
  size_t writeStateJson(char* out, size_t cap, int8_t seat, uint32_t nowMs) const {
    return writeStateJson(out, cap, seat, seat, nowMs);
  }
  // The banner for the shared screen.
  void statusLine(char* out, size_t cap) const;

 private:
  void destroyGame();
  // Keeps a phone as host whenever one is seated; a test seat hosts only a
  // table with no phone at it.
  void rehost();
  void packSeats();
  // Writes the first "<prefix> N" no seated player goes by, trying seat + 1 first.
  void freeName(const char* prefix, uint8_t seat, char* out, size_t cap) const;

  alignas(8) unsigned char storage_[GAME_STORAGE_BYTES] = {};
  Game* game_ = nullptr;

  Player players_[MAX_PLAYERS];
  uint8_t count_ = 0;
  uint8_t hostSeat_ = 0;
  GameId selected_ = GameId::Undercover;
  bool testMode_ = false;
  uint32_t version_ = 1;
  Rng rng_;
};

}  // namespace party
