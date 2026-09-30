#include "Ludo.h"

#include <cstdio>
#include <cstring>

namespace party {

namespace {

// The shared ring, clockwise, as 15x15 board cells. Index 0 is shape 0's start
// square; every shape starts 13 cells further round. Generated once and
// asserted by test/party_games: consecutive cells always touch, and the four
// diagonal steps are the track wrapping the corners of the centre square.
constexpr Ludo::Cell kTrack[Ludo::TRACK] = {
    {0, 6},  {1, 6},  {2, 6},  {3, 6},  {4, 6},  {5, 6}, {6, 5}, {6, 4},  {6, 3},  {6, 2},  {6, 1},  {6, 0},  {7, 0},
    {8, 0},  {8, 1},  {8, 2},  {8, 3},  {8, 4},  {8, 5}, {9, 6}, {10, 6}, {11, 6}, {12, 6}, {13, 6}, {14, 6}, {14, 7},
    {14, 8}, {13, 8}, {12, 8}, {11, 8}, {10, 8}, {9, 8}, {8, 9}, {8, 10}, {8, 11}, {8, 12}, {8, 13}, {8, 14}, {7, 14},
    {6, 14}, {6, 13}, {6, 12}, {6, 11}, {6, 10}, {6, 9}, {5, 8}, {4, 8},  {3, 8},  {2, 8},  {1, 8},  {0, 8},  {0, 7},
};

// The five-cell run into the centre, entered from the ring cell before the
// shape's own start square.
constexpr Ludo::Cell kHome[4][5] = {
    {
        {1, 7},
        {2, 7},
        {3, 7},
        {4, 7},
        {5, 7},
    },
    {
        {7, 1},
        {7, 2},
        {7, 3},
        {7, 4},
        {7, 5},
    },
    {
        {13, 7},
        {12, 7},
        {11, 7},
        {10, 7},
        {9, 7},
    },
    {
        {7, 13},
        {7, 12},
        {7, 11},
        {7, 10},
        {7, 9},
    },
};

// Four parking spots inside each corner yard.
constexpr Ludo::Cell kYard[4][Ludo::TOKENS] = {
    {
        {1, 1},
        {4, 1},
        {1, 4},
        {4, 4},
    },
    {
        {10, 1},
        {13, 1},
        {10, 4},
        {13, 4},
    },
    {
        {10, 10},
        {13, 10},
        {10, 13},
        {13, 13},
    },
    {
        {1, 10},
        {4, 10},
        {1, 13},
        {4, 13},
    },
};

// Own start squares plus the four star squares, eight steps on from each.
constexpr uint8_t kSafe[] = {0, 8, 13, 21, 26, 34, 39, 47};

}  // namespace

Ludo::Cell Ludo::trackCell(const uint8_t index) { return kTrack[index % TRACK]; }

Ludo::Cell Ludo::homeCell(const uint8_t shape, const uint8_t step) { return kHome[shape % SHAPES][step % 5]; }

Ludo::Cell Ludo::yardCell(const uint8_t shape, const uint8_t token) { return kYard[shape % SHAPES][token % TOKENS]; }

uint8_t Ludo::absoluteCell(const uint8_t shape, const int8_t position) {
  return static_cast<uint8_t>((startIndex(shape) + static_cast<uint8_t>(position)) % TRACK);
}

bool Ludo::isSafeCell(const uint8_t trackIndex) {
  for (const uint8_t safe : kSafe) {
    if (safe == trackIndex) return true;
  }
  return false;
}

const char* Ludo::shapeName(const uint8_t shape) {
  static constexpr const char* NAMES[SHAPES] = {"circle", "square", "triangle", "diamond"};
  return shape < SHAPES ? NAMES[shape] : "";
}

void Ludo::start(const uint8_t playerCount, Rng& rng) {
  count_ = playerCount < 2 ? 2 : (playerCount > 4 ? 4 : playerCount);
  for (uint8_t p = 0; p < 4; p++) {
    for (uint8_t t = 0; t < TOKENS; t++) pos_[p][t] = IN_YARD;
    shape_[p] = NO_SHAPE;
    snprintf(names_[p], sizeof(names_[p]), "Seat %u", p + 1);
  }
  turn_ = 0;
  die_ = 0;
  sixStreak_ = 0;
  phase_ = Phase::Pick;
  winner_ = -1;
  rng_.reseed(rng.next());
  snprintf(message_, sizeof(message_), "Pick a shape - it decides your corner");
}

void Ludo::setPlayerName(const uint8_t player, const char* name) {
  if (player >= 4 || !name || !name[0]) return;
  snprintf(names_[player], sizeof(names_[player]), "%s", name);
}

int8_t Ludo::shapeOf(const uint8_t player) const { return player < count_ ? shape_[player] : NO_SHAPE; }

int8_t Ludo::playerWithShape(const uint8_t shape) const {
  if (shape >= SHAPES) return -1;
  for (uint8_t p = 0; p < count_; p++) {
    if (shape_[p] == static_cast<int8_t>(shape)) return static_cast<int8_t>(p);
  }
  return -1;
}

int8_t Ludo::position(const uint8_t player, const uint8_t token) const {
  if (player >= count_ || token >= TOKENS) return IN_YARD;
  return pos_[player][token];
}

uint8_t Ludo::tokensHome(const uint8_t player) const {
  if (player >= count_) return 0;
  uint8_t done = 0;
  for (uint8_t t = 0; t < TOKENS; t++) {
    if (pos_[player][t] == static_cast<int8_t>(HOME)) done++;
  }
  return done;
}

bool Ludo::canMove(const uint8_t player, const uint8_t token, const uint8_t die) const {
  const int8_t at = pos_[player][token];
  if (at == static_cast<int8_t>(HOME)) return false;
  if (at == IN_YARD) return die == 6;
  // The home square has to be reached exactly.
  return at + static_cast<int8_t>(die) <= static_cast<int8_t>(HOME);
}

uint8_t Ludo::legalMoves(const uint8_t player, const uint8_t die, uint8_t* out) const {
  uint8_t found = 0;
  if (player >= count_ || die == 0) return 0;
  for (uint8_t t = 0; t < TOKENS; t++) {
    if (canMove(player, t, die)) out[found++] = t;
  }
  return found;
}

bool Ludo::captureAt(const uint8_t mover, const uint8_t trackIndex) {
  if (isSafeCell(trackIndex)) return false;
  bool captured = false;
  for (uint8_t p = 0; p < count_; p++) {
    if (p == mover || shape_[p] < 0) continue;
    for (uint8_t t = 0; t < TOKENS; t++) {
      const int8_t at = pos_[p][t];
      if (at < 0 || at > static_cast<int8_t>(LAST_TRACK)) continue;
      if (absoluteCell(static_cast<uint8_t>(shape_[p]), at) != trackIndex) continue;
      pos_[p][t] = IN_YARD;
      captured = true;
    }
  }
  return captured;
}

bool Ludo::everyonePicked() const {
  for (uint8_t p = 0; p < count_; p++) {
    if (shape_[p] < 0) return false;
  }
  return true;
}

uint8_t Ludo::nextByShape(const uint8_t player) const {
  // Clockwise round the board: the next shape up that someone holds.
  for (uint8_t step = 1; step <= SHAPES; step++) {
    const int8_t next = playerWithShape(static_cast<uint8_t>((shape_[player] + step) % SHAPES));
    if (next >= 0) return static_cast<uint8_t>(next);
  }
  return player;
}

void Ludo::beginPlay(const char* pickNote) {
  for (uint8_t shape = 0; shape < SHAPES; shape++) {
    const int8_t holder = playerWithShape(shape);
    if (holder < 0) continue;
    turn_ = static_cast<uint8_t>(holder);
    break;
  }
  die_ = 0;
  sixStreak_ = 0;
  phase_ = Phase::Roll;
  snprintf(message_, sizeof(message_), "%s - %s rolls first", pickNote, names_[turn_]);
}

bool Ludo::applyPick(const uint8_t seat, const Action& action, const bool isHost) {
  switch (action.verb) {
    // Its own verb: the lobby's game Pick must never land here, nor a stale
    // shape pick in the lobby.
    case Verb::Shape: {
      if (seat >= count_ || action.a < 0 || action.a >= SHAPES) return false;
      const uint8_t shape = static_cast<uint8_t>(action.a);
      // A free shape only; re-picking a player's own shape changes nothing.
      if (playerWithShape(shape) >= 0) return false;
      shape_[seat] = static_cast<int8_t>(shape);
      char note[40];
      snprintf(note, sizeof(note), "%s took the %s", names_[seat], shapeName(shape));
      if (everyonePicked()) {
        beginPlay(note);
      } else {
        snprintf(message_, sizeof(message_), "%s", note);
      }
      return true;
    }

    case Verb::Next: {
      // The host (the device's Select) deals the free shapes out in seat order
      // so a table never stalls on someone who has not picked.
      if (!isHost) return false;
      for (uint8_t p = 0; p < count_; p++) {
        if (shape_[p] >= 0) continue;
        for (uint8_t shape = 0; shape < SHAPES; shape++) {
          if (playerWithShape(shape) >= 0) continue;
          shape_[p] = static_cast<int8_t>(shape);
          break;
        }
      }
      beginPlay("Shapes dealt");
      return true;
    }

    default:
      return false;
  }
}

void Ludo::advanceTurn() {
  turn_ = nextByShape(turn_);
  sixStreak_ = 0;
  phase_ = Phase::Roll;
}

void Ludo::finishRoll() {
  uint8_t moves[TOKENS];
  if (legalMoves(turn_, die_, moves) == 0) {
    snprintf(message_, sizeof(message_), "%s rolled %u - nothing to move", names_[turn_], die_);
    advanceTurn();
    return;
  }
  phase_ = Phase::Move;
  snprintf(message_, sizeof(message_), "%s rolled %u", names_[turn_], die_);
}

bool Ludo::apply(const uint8_t seat, const Action& action, const bool isHost) {
  if (phase_ == Phase::Over) return false;
  if (phase_ == Phase::Pick) return applyPick(seat, action, isHost);
  // From here on every phase is gated by whose turn it is.
  if (seat >= count_ || seat != turn_) return false;

  switch (action.verb) {
    case Verb::Roll: {
      if (phase_ != Phase::Roll) return false;
      die_ = static_cast<uint8_t>(1 + rng_.below(6));
      if (die_ == 6) {
        sixStreak_++;
        if (sixStreak_ >= 3) {
          // Three sixes in a row forfeits the turn, or a lucky streak never ends.
          snprintf(message_, sizeof(message_), "%s rolled three sixes - turn lost", names_[turn_]);
          advanceTurn();
          return true;
        }
      }
      finishRoll();
      return true;
    }

    case Verb::Move: {
      if (phase_ != Phase::Move) return false;
      const int16_t token = action.a;
      if (token < 0 || token >= TOKENS) return false;
      if (!canMove(seat, static_cast<uint8_t>(token), die_)) return false;

      int8_t& at = pos_[seat][token];
      if (at == IN_YARD) {
        at = 0;
      } else {
        at = static_cast<int8_t>(at + die_);
      }

      bool extraTurn = die_ == 6;
      const uint8_t shape = static_cast<uint8_t>(shape_[seat]);
      if (at <= static_cast<int8_t>(LAST_TRACK) && captureAt(seat, absoluteCell(shape, at))) {
        snprintf(message_, sizeof(message_), "%s sends a token home - roll again", names_[seat]);
        extraTurn = true;
      } else if (at == static_cast<int8_t>(HOME)) {
        snprintf(message_, sizeof(message_), "%s brings a token home (%u of 4)", names_[seat], tokensHome(seat));
        extraTurn = true;
      }

      if (tokensHome(seat) == TOKENS) {
        winner_ = static_cast<int8_t>(seat);
        phase_ = Phase::Over;
        snprintf(message_, sizeof(message_), "%s is home with all four", names_[seat]);
        return true;
      }

      if (extraTurn) {
        phase_ = Phase::Roll;
        if (die_ != 6) sixStreak_ = 0;  // only a six extends the streak
      } else {
        advanceTurn();
      }
      return true;
    }

    default:
      return false;
  }
}

void Ludo::statusLine(char* out, const size_t cap) const {
  switch (phase_) {
    case Phase::Pick: {
      uint8_t picked = 0;
      for (uint8_t p = 0; p < count_; p++) {
        if (shape_[p] >= 0) picked++;
      }
      snprintf(out, cap, "Pick a shape - %u of %u chosen", picked, count_);
      return;
    }
    case Phase::Roll:
      snprintf(out, cap, "%s to roll - %s", names_[turn_], message_);
      return;
    case Phase::Move:
      snprintf(out, cap, "%s rolled %u - pick a token", names_[turn_], die_);
      return;
    case Phase::Over:
      snprintf(out, cap, "%s", message_);
      return;
  }
}

void Ludo::writeView(JsonBuf& out, const int8_t seat) const {
  static constexpr const char* PHASES[] = {"pick", "roll", "move", "over"};
  const bool playing = seat >= 0 && seat < static_cast<int8_t>(count_);
  const bool myTurn = playing && phase_ != Phase::Pick && seat == static_cast<int8_t>(turn_);

  out.ch('{');
  out.keyStr("phase", PHASES[static_cast<uint8_t>(phase_)]);
  out.ch(',');
  out.keyNum("turn", turn_);
  out.ch(',');
  out.keyNum("me", playing ? seat : -1);
  out.ch(',');
  out.keyNum("die", die_);
  out.ch(',');
  out.keyNum("winner", winner_);
  out.ch(',');
  out.keyStr("msg", message_);
  out.ch(',');
  out.keyBool("myTurn", myTurn);
  out.ch(',');

  out.key("moves");
  out.ch('[');
  if (myTurn && phase_ == Phase::Move) {
    uint8_t moves[TOKENS];
    const uint8_t found = legalMoves(turn_, die_, moves);
    for (uint8_t i = 0; i < found; i++) {
      if (i) out.ch(',');
      out.num(moves[i]);
    }
  }
  out.ch(']');
  out.ch(',');

  out.key("tokens");
  out.ch('[');
  for (uint8_t p = 0; p < count_; p++) {
    if (p) out.ch(',');
    out.ch('[');
    for (uint8_t t = 0; t < TOKENS; t++) {
      if (t) out.ch(',');
      out.num(pos_[p][t]);
    }
    out.ch(']');
  }
  out.ch(']');
  out.ch(',');

  out.key("shapes");
  out.ch('[');
  for (uint8_t p = 0; p < count_; p++) {
    if (p) out.ch(',');
    out.num(shape_[p]);
  }
  out.ch(']');
  out.ch('}');
}

}  // namespace party
