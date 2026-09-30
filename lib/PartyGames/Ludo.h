#pragma once

#include "Game.h"

namespace party {

// Ludo for two to four seats. The phones are the dice cups and the token
// pickers; the 15x15 board only exists on the shared screen.
//
// Each player plays a shape rather than a seat colour: circle, square, triangle
// or diamond, picked on the phone (Verb::Shape) before the first roll. Shape s
// owns board corner s (0 top left, then clockwise), its start square and its
// home column, so every piece of geometry below is indexed by shape. Token
// positions stay per player (the dealt seats 0..players()-1).
class Ludo final : public Game {
 public:
  enum class Phase : uint8_t { Pick, Roll, Move, Over };

  static constexpr uint8_t TOKENS = 4;
  static constexpr uint8_t SHAPES = 4;       // 0 circle, 1 square, 2 triangle, 3 diamond
  static constexpr int8_t NO_SHAPE = -1;     // not picked yet / nobody holds it
  static constexpr uint8_t TRACK = 52;       // shared ring cells
  static constexpr uint8_t GRID = 15;        // board is 15 x 15 cells
  static constexpr int8_t IN_YARD = -1;      // token has not entered yet
  static constexpr uint8_t LAST_TRACK = 51;  // final shared-ring step
  static constexpr uint8_t HOME = 57;        // 52..56 are the home column, 57 is home

  struct Cell {
    uint8_t col;
    uint8_t row;
  };

  // Board geometry by shape, shared with the renderer and asserted by the host
  // tests.
  static Cell trackCell(uint8_t index);                // 0..51
  static Cell homeCell(uint8_t shape, uint8_t step);   // step 0..4
  static Cell yardCell(uint8_t shape, uint8_t token);  // 0..3
  static uint8_t startIndex(uint8_t shape) { return static_cast<uint8_t>(shape * 13); }
  static uint8_t absoluteCell(uint8_t shape, int8_t position);  // position 0..51 -> ring index
  static bool isSafeCell(uint8_t trackIndex);
  // English shape name, for the phones' message line.
  static const char* shapeName(uint8_t shape);

  GameId id() const override { return GameId::Ludo; }
  void start(uint8_t playerCount, Rng& rng) override;
  bool apply(uint8_t seat, const Action& action, bool isHost) override;
  bool isOver() const override { return phase_ == Phase::Over; }
  void statusLine(char* out, size_t cap) const override;
  void writeView(JsonBuf& out, int8_t seat) const override;

  // Names for the message line; a player without one reads as "Seat N".
  void setPlayerName(uint8_t player, const char* name);

  // Shared-screen accessors.
  Phase phase() const { return phase_; }
  uint8_t players() const { return count_; }
  int8_t shapeOf(uint8_t player) const;         // NO_SHAPE until picked
  int8_t playerWithShape(uint8_t shape) const;  // -1 while the shape is free
  int8_t position(uint8_t player, uint8_t token) const;
  uint8_t turn() const { return turn_; }
  uint8_t die() const { return die_; }
  int8_t winner() const { return winner_; }
  uint8_t tokensHome(uint8_t player) const;
  const char* message() const { return message_; }
  // Fills `out` (at least TOKENS entries) with the movable tokens for the
  // current roll and returns how many there are.
  uint8_t legalMoves(uint8_t player, uint8_t die, uint8_t* out) const;

 private:
  bool applyPick(uint8_t seat, const Action& action, bool isHost);
  bool everyonePicked() const;
  void beginPlay(const char* pickNote);
  bool canMove(uint8_t player, uint8_t token, uint8_t die) const;
  bool captureAt(uint8_t mover, uint8_t trackIndex);
  uint8_t nextByShape(uint8_t player) const;
  void advanceTurn();
  void finishRoll();

  uint8_t count_ = 0;
  int8_t pos_[4][TOKENS] = {};
  int8_t shape_[4] = {NO_SHAPE, NO_SHAPE, NO_SHAPE, NO_SHAPE};
  char names_[4][MAX_NAME_LEN + 1] = {};
  uint8_t turn_ = 0;
  uint8_t die_ = 0;
  uint8_t sixStreak_ = 0;
  Phase phase_ = Phase::Pick;
  int8_t winner_ = -1;
  char message_[64] = {};
  Rng rng_;
};

}  // namespace party
