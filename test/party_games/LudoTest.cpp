#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <set>
#include <string>

#include "GameSession.h"
#include "Ludo.h"
#include "PartyGames.h"

using namespace party;

namespace {

Action verb(const Verb v, const int16_t a = -1) {
  Action action;
  action.verb = v;
  action.a = a;
  return action;
}

// Player i picks the i-th shape listed; the last pick opens the first roll.
void pick(Ludo& game, const std::initializer_list<uint8_t> shapes) {
  uint8_t player = 0;
  for (const uint8_t shape : shapes) {
    ASSERT_TRUE(game.apply(player, verb(Verb::Shape, shape), false)) << "player " << static_cast<int>(player);
    player++;
  }
  ASSERT_EQ(game.phase(), Ludo::Phase::Roll);
}

// Every player on the shape of its own index: corner and turn order match the
// seat order.
void pickInSeatOrder(Ludo& game) {
  for (uint8_t player = 0; player < game.players(); player++) {
    ASSERT_TRUE(game.apply(player, verb(Verb::Shape, player), false));
  }
  ASSERT_EQ(game.phase(), Ludo::Phase::Roll);
}

std::string view(const Ludo& game, const int8_t seat) {
  char buf[1024];
  JsonBuf json(buf, sizeof(buf));
  game.writeView(json, seat);
  return std::string(buf, json.size());
}

// The first dice a round seeded with `seed` throws: the game reseeds its own
// generator from the caller's first draw, so a mirror predicts them.
bool firstRollsAvoidSix(const uint32_t seed, const int rolls) {
  Rng seeder(seed);
  Rng mirror(seeder.next());
  for (int i = 0; i < rolls; i++) {
    if (1 + mirror.below(6) == 6) return false;
  }
  return true;
}

int8_t boardCell(const Ludo& game, const uint8_t player, const uint8_t token) {
  const int8_t at = game.position(player, token);
  if (at < 0 || at > static_cast<int8_t>(Ludo::LAST_TRACK)) return -1;
  return static_cast<int8_t>(Ludo::absoluteCell(static_cast<uint8_t>(game.shapeOf(player)), at));
}

}  // namespace

// --- board -------------------------------------------------------------------

TEST(Ludo, BoardGeometryIsAClosedRing) {
  std::set<std::pair<int, int>> unique;
  for (uint8_t i = 0; i < Ludo::TRACK; i++) {
    const Ludo::Cell cell = Ludo::trackCell(i);
    EXPECT_LT(cell.col, Ludo::GRID);
    EXPECT_LT(cell.row, Ludo::GRID);
    unique.insert({cell.col, cell.row});
    const Ludo::Cell next = Ludo::trackCell(static_cast<uint8_t>((i + 1) % Ludo::TRACK));
    const int dx = static_cast<int>(next.col) - static_cast<int>(cell.col);
    const int dy = static_cast<int>(next.row) - static_cast<int>(cell.row);
    EXPECT_LE(abs(dx), 1) << "step " << static_cast<int>(i);
    EXPECT_LE(abs(dy), 1) << "step " << static_cast<int>(i);
    EXPECT_FALSE(dx == 0 && dy == 0);
  }
  EXPECT_EQ(unique.size(), Ludo::TRACK);

  for (uint8_t shape = 0; shape < Ludo::SHAPES; shape++) {
    EXPECT_EQ(Ludo::startIndex(shape), shape * 13);
    EXPECT_TRUE(Ludo::isSafeCell(Ludo::startIndex(shape))) << "a start square shelters its owner";
    // The home column is entered from the ring cell before the shape's start.
    const Ludo::Cell entry = Ludo::trackCell(static_cast<uint8_t>((Ludo::startIndex(shape) + 51) % Ludo::TRACK));
    const Ludo::Cell first = Ludo::homeCell(shape, 0);
    const int dx = abs(static_cast<int>(entry.col) - static_cast<int>(first.col));
    const int dy = abs(static_cast<int>(entry.row) - static_cast<int>(first.row));
    EXPECT_EQ(dx + dy, 1) << "shape " << static_cast<int>(shape) << " turns into its column";
    for (uint8_t step = 0; step < 5; step++) {
      const Ludo::Cell home = Ludo::homeCell(shape, step);
      EXPECT_EQ(unique.count({home.col, home.row}), 0u) << "home cells are off the shared ring";
    }
  }
}

// --- the shape pick ----------------------------------------------------------

TEST(Ludo, ARoundOpensWithTheShapePick) {
  Ludo game;
  Rng rng(40);
  game.start(3, rng);
  EXPECT_EQ(game.phase(), Ludo::Phase::Pick);
  for (uint8_t player = 0; player < 3; player++) EXPECT_EQ(game.shapeOf(player), Ludo::NO_SHAPE);
  for (uint8_t shape = 0; shape < Ludo::SHAPES; shape++) EXPECT_EQ(game.playerWithShape(shape), -1);
  EXPECT_FALSE(game.apply(0, verb(Verb::Roll), false)) << "no dice before everyone has a corner";
  EXPECT_FALSE(game.apply(0, verb(Verb::Move, 0), false));
}

TEST(Ludo, APlayerPicksSwitchesAndCannotTakeATakenShape) {
  Ludo game;
  Rng rng(41);
  game.start(3, rng);
  game.setPlayerName(0, "Ann");
  game.setPlayerName(1, "Bo");
  game.setPlayerName(2, "Cy");

  ASSERT_TRUE(game.apply(0, verb(Verb::Shape, 2), false));
  EXPECT_EQ(game.shapeOf(0), 2);
  EXPECT_EQ(game.playerWithShape(2), 0);
  EXPECT_STREQ(game.message(), "Ann took the triangle");

  EXPECT_FALSE(game.apply(1, verb(Verb::Shape, 2), false)) << "the triangle is taken";
  EXPECT_FALSE(game.apply(0, verb(Verb::Shape, 2), false)) << "re-picking your own shape changes nothing";
  EXPECT_FALSE(game.apply(1, verb(Verb::Shape, 4), false));
  EXPECT_FALSE(game.apply(1, verb(Verb::Shape, -1), false));
  EXPECT_FALSE(game.apply(3, verb(Verb::Shape, 1), false)) << "a seat that was not dealt in";

  ASSERT_TRUE(game.apply(0, verb(Verb::Shape, 3), false)) << "switching while the pick is open";
  EXPECT_EQ(game.shapeOf(0), 3);
  EXPECT_EQ(game.playerWithShape(2), -1) << "the old shape is free again";
  ASSERT_TRUE(game.apply(1, verb(Verb::Shape, 2), false));
  EXPECT_EQ(game.phase(), Ludo::Phase::Pick);

  ASSERT_TRUE(game.apply(2, verb(Verb::Shape, 0), false));
  EXPECT_EQ(game.phase(), Ludo::Phase::Roll) << "the last pick opens the dice";
  EXPECT_STREQ(game.message(), "Cy took the circle - Cy rolls first");
  EXPECT_FALSE(game.apply(0, verb(Verb::Shape, 1), false)) << "shapes are fixed once the dice roll";
}

TEST(Ludo, TheHostDealsTheFreeShapes) {
  Ludo game;
  Rng rng(42);
  game.start(4, rng);
  ASSERT_TRUE(game.apply(2, verb(Verb::Shape, 0), false));

  EXPECT_FALSE(game.apply(1, verb(Verb::Next), false)) << "only the host deals";
  // The host may be watching from a seat past the four dealt in.
  ASSERT_TRUE(game.apply(5, verb(Verb::Next), true));
  EXPECT_EQ(game.phase(), Ludo::Phase::Roll);
  EXPECT_EQ(game.shapeOf(0), 1) << "free shapes go out ascending, in seat order";
  EXPECT_EQ(game.shapeOf(1), 2);
  EXPECT_EQ(game.shapeOf(2), 0) << "a shape already picked is kept";
  EXPECT_EQ(game.shapeOf(3), 3);
  EXPECT_EQ(game.turn(), 2) << "the circle rolls first";
  EXPECT_STREQ(game.message(), "Shapes dealt - Seat 3 rolls first");
  // Asked of the seat on turn, so the turn gate is not what refuses it.
  EXPECT_FALSE(game.apply(game.turn(), verb(Verb::Next), true)) << "Next means nothing once the dice roll";
  EXPECT_FALSE(game.apply(game.turn(), verb(Verb::Shape, 1), true)) << "nor does a shape";
  EXPECT_EQ(game.phase(), Ludo::Phase::Roll);
}

TEST(Ludo, TurnsGoRoundTheShapesClockwise) {
  uint32_t seed = 1;
  while (!firstRollsAvoidSix(seed, 4)) seed++;

  Ludo game;
  Rng rng(seed);
  game.start(3, rng);
  pick(game, {3, 1, 2});
  EXPECT_EQ(game.turn(), 1) << "the lowest shape held rolls first";
  EXPECT_FALSE(game.apply(0, verb(Verb::Roll), false));

  // No six, so every roll finds nothing to move and passes the dice on.
  const uint8_t order[] = {1, 2, 0, 1};
  for (int i = 0; i < 3; i++) {
    ASSERT_EQ(game.turn(), order[i]);
    ASSERT_TRUE(game.apply(order[i], verb(Verb::Roll), false));
    EXPECT_EQ(game.turn(), order[i + 1]) << "square, triangle, diamond, then round again";
  }
}

TEST(Ludo, ATriangleEntersAtItsOwnStartAndCapturesByBoardCell) {
  bool entered = false;
  int captures = 0;
  for (uint32_t seed = 1; seed < 200 && (!entered || captures == 0); seed++) {
    Ludo game;
    Rng rng(seed);
    game.start(2, rng);
    pick(game, {2, 0});  // player 0 on the triangle, player 1 on the circle

    for (int guard = 0; guard < 4000 && !game.isOver(); guard++) {
      const uint8_t player = game.turn();
      ASSERT_TRUE(game.apply(player, verb(Verb::Roll), false));
      if (game.phase() != Ludo::Phase::Move) continue;

      uint8_t moves[Ludo::TOKENS];
      ASSERT_GT(game.legalMoves(player, game.die(), moves), 0);
      const uint8_t token = moves[0];
      const bool fromYard = game.position(player, token) == Ludo::IN_YARD;
      const uint8_t other = static_cast<uint8_t>(1 - player);
      int8_t before[Ludo::TOKENS];
      for (uint8_t t = 0; t < Ludo::TOKENS; t++) before[t] = boardCell(game, other, t);

      ASSERT_TRUE(game.apply(player, verb(Verb::Move, token), false));

      if (fromYard && player == 0) {
        EXPECT_EQ(boardCell(game, 0, token), Ludo::startIndex(2)) << "the triangle's corner, not seat 0's";
        entered = true;
      }
      const int8_t landed = boardCell(game, player, token);
      for (uint8_t t = 0; t < Ludo::TOKENS; t++) {
        if (before[t] < 0 || game.position(other, t) != Ludo::IN_YARD) continue;
        EXPECT_EQ(before[t], landed) << "a capture happens on the square the mover landed on";
        captures++;
      }
    }
  }
  EXPECT_TRUE(entered);
  EXPECT_GT(captures, 0) << "no capture in 200 two-player games means the rule is dead";
}

// --- play --------------------------------------------------------------------

TEST(Ludo, ASixIsTheOnlyWayOutOfTheYard) {
  Ludo game;
  Rng rng(43);
  game.start(4, rng);
  pickInSeatOrder(game);
  uint8_t moves[Ludo::TOKENS];
  for (uint8_t die = 1; die <= 5; die++) {
    EXPECT_EQ(game.legalMoves(0, die, moves), 0) << "die " << static_cast<int>(die);
  }
  EXPECT_EQ(game.legalMoves(0, 6, moves), Ludo::TOKENS);
}

TEST(Ludo, NoLegalMovePassesTheTurn) {
  Ludo game;
  Rng rng(44);
  game.start(4, rng);
  pickInSeatOrder(game);
  for (int guard = 0; guard < 200; guard++) {
    const uint8_t before = game.turn();
    ASSERT_TRUE(game.apply(before, verb(Verb::Roll), false));
    if (game.die() == 6) {
      EXPECT_EQ(game.phase(), Ludo::Phase::Move);
      return;
    }
    EXPECT_EQ(game.phase(), Ludo::Phase::Roll);
    EXPECT_EQ(game.turn(), (before + 1) % 4) << "a useless roll hands the dice on";
  }
  FAIL() << "no six in 200 rolls";
}

TEST(Ludo, ThreeSixesForfeitTheTurn) {
  // Mirror the game's generator to find a seed whose first three rolls are all
  // sixes.
  for (uint32_t seed = 1; seed < 20000; seed++) {
    Rng seeder(seed);
    Rng mirror(seeder.next());
    if (1 + mirror.below(6) != 6) continue;
    if (1 + mirror.below(6) != 6) continue;
    if (1 + mirror.below(6) != 6) continue;

    Ludo game;
    Rng rng(seed);
    game.start(4, rng);
    pickInSeatOrder(game);
    ASSERT_TRUE(game.apply(0, verb(Verb::Roll), false));
    ASSERT_EQ(game.die(), 6);
    ASSERT_TRUE(game.apply(0, verb(Verb::Move, 0), false));
    EXPECT_EQ(game.turn(), 0) << "a six is played again";
    ASSERT_TRUE(game.apply(0, verb(Verb::Roll), false));
    ASSERT_TRUE(game.apply(0, verb(Verb::Move, 1), false));
    ASSERT_EQ(game.turn(), 0);
    ASSERT_TRUE(game.apply(0, verb(Verb::Roll), false));
    EXPECT_EQ(game.turn(), 1) << "the third six is forfeited";
    EXPECT_EQ(game.phase(), Ludo::Phase::Roll);
    return;
  }
  FAIL() << "no seed produced three sixes";
}

TEST(Ludo, ThreeSixesForfeitWithShapesOutOfSeatOrder) {
  for (uint32_t seed = 1; seed < 20000; seed++) {
    Rng seeder(seed);
    Rng mirror(seeder.next());
    if (1 + mirror.below(6) != 6) continue;
    if (1 + mirror.below(6) != 6) continue;
    if (1 + mirror.below(6) != 6) continue;

    Ludo game;
    Rng rng(seed);
    game.start(4, rng);
    pick(game, {2, 0, 3, 1});  // the circle is player 1, the square player 3
    const uint8_t first = game.turn();
    ASSERT_EQ(first, 1) << "the circle rolls first";
    ASSERT_TRUE(game.apply(first, verb(Verb::Roll), false));
    ASSERT_TRUE(game.apply(first, verb(Verb::Move, 0), false));
    ASSERT_TRUE(game.apply(first, verb(Verb::Roll), false));
    ASSERT_TRUE(game.apply(first, verb(Verb::Move, 1), false));
    ASSERT_EQ(game.turn(), first);
    ASSERT_TRUE(game.apply(first, verb(Verb::Roll), false));
    EXPECT_EQ(game.turn(), 3) << "the dice pass to the square, not to seat 2";
    EXPECT_EQ(game.phase(), Ludo::Phase::Roll);
    EXPECT_EQ(game.position(first, 2), Ludo::IN_YARD) << "the third six moved nothing";
    return;
  }
  FAIL() << "no seed produced three sixes";
}

TEST(Ludo, AFullGameKeepsItsInvariantsAndEnds) {
  Ludo game;
  Rng rng(45);
  game.start(4, rng);
  // Shapes out of seat order, so a seat index standing in for a shape shows.
  pick(game, {2, 0, 3, 1});

  int captures = 0;
  int homed = 0;
  for (int guard = 0; guard < 20000 && !game.isOver(); guard++) {
    const uint8_t player = game.turn();
    ASSERT_TRUE(game.apply(player, verb(Verb::Roll), false));
    if (game.phase() != Ludo::Phase::Move) continue;

    uint8_t moves[Ludo::TOKENS];
    const uint8_t options = game.legalMoves(player, game.die(), moves);
    ASSERT_GT(options, 0);
    int yardBefore = 0;
    int homeBefore = 0;
    for (uint8_t p = 0; p < 4; p++) {
      for (uint8_t t = 0; t < Ludo::TOKENS; t++) {
        if (p != player && game.position(p, t) == Ludo::IN_YARD) yardBefore++;
      }
      if (p == player) homeBefore = game.tokensHome(p);
    }
    ASSERT_TRUE(game.apply(player, verb(Verb::Move, moves[0]), false));

    int yardAfter = 0;
    for (uint8_t p = 0; p < 4; p++) {
      for (uint8_t t = 0; t < Ludo::TOKENS; t++) {
        const int8_t at = game.position(p, t);
        ASSERT_GE(at, Ludo::IN_YARD);
        ASSERT_LE(at, static_cast<int8_t>(Ludo::HOME));
        if (p != player && at == Ludo::IN_YARD) yardAfter++;
      }
    }
    if (yardAfter > yardBefore) captures++;
    if (game.tokensHome(player) > homeBefore) homed++;

    // No two players ever share an unsafe ring cell once a move resolves.
    for (uint8_t a = 0; a < 4; a++) {
      for (uint8_t ta = 0; ta < Ludo::TOKENS; ta++) {
        const int8_t cellA = boardCell(game, a, ta);
        if (cellA < 0 || Ludo::isSafeCell(static_cast<uint8_t>(cellA))) continue;
        for (uint8_t b = static_cast<uint8_t>(a + 1); b < 4; b++) {
          for (uint8_t tb = 0; tb < Ludo::TOKENS; tb++) {
            EXPECT_NE(boardCell(game, b, tb), cellA) << "an unsafe square held two shapes";
          }
        }
      }
    }
  }

  EXPECT_TRUE(game.isOver());
  ASSERT_GE(game.winner(), 0);
  EXPECT_EQ(game.tokensHome(static_cast<uint8_t>(game.winner())), Ludo::TOKENS);
  EXPECT_GT(captures, 0) << "a 20000 move game with no capture means the rule is dead";
  EXPECT_GT(homed, 0);
}

TEST(Ludo, OnlyTheSeatOnTurnCanAct) {
  Ludo game;
  Rng rng(46);
  game.start(3, rng);
  pickInSeatOrder(game);
  EXPECT_FALSE(game.apply(1, verb(Verb::Roll), false));
  EXPECT_FALSE(game.apply(2, verb(Verb::Move, 0), false));
  EXPECT_TRUE(game.apply(0, verb(Verb::Roll), false));
}

// --- views -------------------------------------------------------------------

TEST(Ludo, TheViewCarriesTheShapes) {
  Ludo game;
  Rng rng(47);
  game.start(3, rng);

  std::string doc = view(game, -1);
  EXPECT_NE(doc.find("\"phase\":\"pick\""), std::string::npos) << doc;
  EXPECT_NE(doc.find("\"shapes\":[-1,-1,-1]"), std::string::npos) << doc;
  EXPECT_NE(doc.find("\"myTurn\":false"), std::string::npos) << "nobody's turn while picking";

  ASSERT_TRUE(game.apply(0, verb(Verb::Shape, 2), false));
  ASSERT_TRUE(game.apply(1, verb(Verb::Shape, 0), false));
  doc = view(game, -1);
  EXPECT_NE(doc.find("\"shapes\":[2,0,-1]"), std::string::npos) << doc;

  ASSERT_TRUE(game.apply(2, verb(Verb::Shape, 1), false));
  doc = view(game, 1);
  EXPECT_NE(doc.find("\"phase\":\"roll\""), std::string::npos) << doc;
  EXPECT_NE(doc.find("\"shapes\":[2,0,1]"), std::string::npos) << doc;
  EXPECT_NE(doc.find("\"turn\":1"), std::string::npos) << "the circle's player rolls first";
  EXPECT_NE(doc.find("\"me\":1"), std::string::npos) << doc;
  EXPECT_NE(doc.find("\"myTurn\":true"), std::string::npos) << doc;
  for (const char* key : {"\"die\":", "\"winner\":", "\"msg\":", "\"moves\":[", "\"tokens\":[[-1,-1,-1,-1]"}) {
    EXPECT_NE(doc.find(key), std::string::npos) << key << " went missing from " << doc;
  }
}

TEST(Ludo, TheSessionRoutesThePickToTheRound) {
  GameSession session(48);
  for (uint32_t token = 1; token <= 3; token++) {
    char name[8];
    snprintf(name, sizeof(name), "P%u", static_cast<unsigned>(token));
    ASSERT_GE(session.join(name, token, 0), 0);
  }
  session.selectGame(GameId::Ludo);
  ASSERT_TRUE(session.startGame());
  const auto* game = static_cast<const Ludo*>(session.game());
  ASSERT_EQ(game->phase(), Ludo::Phase::Pick);

  EXPECT_FALSE(session.applyAction(1, verb(Verb::Pick, 2))) << "the lobby's game pick never reaches the round";
  EXPECT_EQ(game->shapeOf(1), Ludo::NO_SHAPE);
  EXPECT_TRUE(session.applyAction(1, verb(Verb::Shape, 2))) << "a phone's shape reaches the round";
  EXPECT_EQ(game->shapeOf(1), 2);
  const uint8_t host = static_cast<uint8_t>(session.hostSeat());
  EXPECT_TRUE(session.applyAction(host, verb(Verb::Next))) << "the device's Select deals the rest";
  EXPECT_EQ(game->phase(), Ludo::Phase::Roll);

  char doc[3072];
  session.writeStateJson(doc, sizeof(doc), 0, 1000);
  EXPECT_NE(std::string(doc).find("\"shapes\":["), std::string::npos) << doc;
}

TEST(Ludo, AStaleShapeAfterTheRoundEndsChangesNothing) {
  GameSession session(49);
  for (uint32_t token = 1; token <= 2; token++) {
    char name[8];
    snprintf(name, sizeof(name), "P%u", static_cast<unsigned>(token));
    ASSERT_GE(session.join(name, token, 0), 0);
  }
  session.selectGame(GameId::Ludo);
  ASSERT_TRUE(session.startGame());
  const uint8_t host = static_cast<uint8_t>(session.hostSeat());
  ASSERT_TRUE(session.applyAction(host, verb(Verb::End)));
  ASSERT_TRUE(session.inLobby());

  // The host's phone was mid-pick when the round ended.
  const uint32_t before = session.version();
  EXPECT_FALSE(session.applyAction(host, verb(Verb::Shape, 0))) << "no round to take a shape";
  EXPECT_EQ(session.selected(), GameId::Ludo) << "and it never reads as a game pick";
  EXPECT_EQ(session.version(), before);
}
