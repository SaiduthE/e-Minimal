#include <gtest/gtest.h>

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>

#include "Battleship.h"
#include "Codenames.h"
#include "GameSession.h"
#include "Ludo.h"
#include "PartyGames.h"
#include "Undercover.h"
#include "WordBank.h"

using namespace party;

namespace {

Action verb(const Verb v, const int16_t a = -1, const int16_t b = -1, const int16_t c = -1,
            const char* text = nullptr) {
  Action action;
  action.verb = v;
  action.a = a;
  action.b = b;
  action.c = c;
  if (text) snprintf(action.text, sizeof(action.text), "%s", text);
  return action;
}

// Seats `count` players and returns the session's tokens (token == seat + 1).
void seat(GameSession& session, const uint8_t count) {
  for (uint8_t i = 0; i < count; i++) {
    char name[8];
    snprintf(name, sizeof(name), "P%u", i + 1);
    ASSERT_EQ(session.join(name, i + 1u, 0), static_cast<int8_t>(i));
  }
}

}  // namespace

// --- primitives --------------------------------------------------------------

TEST(Rng, IsDeterministicAndBounded) {
  Rng a(12345);
  Rng b(12345);
  for (int i = 0; i < 1000; i++) {
    const uint32_t value = a.next();
    EXPECT_EQ(value, b.next());
  }
  Rng c(7);
  for (int i = 0; i < 1000; i++) EXPECT_LT(c.below(6), 6u);
  EXPECT_EQ(c.below(0), 0u);
}

TEST(Rng, ShufflePreservesTheMultiset) {
  uint8_t values[8];
  for (uint8_t i = 0; i < 8; i++) values[i] = i;
  Rng rng(99);
  rng.shuffle(values, 8);
  std::set<uint8_t> seen(values, values + 8);
  EXPECT_EQ(seen.size(), 8u);
}

TEST(SanitizeName, DropsWhatWouldBreakTheViews) {
  char out[MAX_NAME_LEN + 1];
  EXPECT_EQ(sanitizeName("An\"na\\", out, sizeof(out)), 4u);
  EXPECT_STREQ(out, "Anna");
  EXPECT_EQ(sanitizeName("<script>", out, sizeof(out)), 6u);
  EXPECT_STREQ(out, "script");
  sanitizeName("averylongplayername", out, sizeof(out));
  EXPECT_EQ(strlen(out), MAX_NAME_LEN);
  EXPECT_EQ(sanitizeName("   ", out, sizeof(out)), 0u);
  EXPECT_EQ(sanitizeName(nullptr, out, sizeof(out)), 0u);
}

TEST(JsonBuf, EscapesAndFlagsTruncation) {
  char small[8];
  JsonBuf tiny(small, sizeof(small));
  tiny.str("aaaaaaaaaaaa");
  EXPECT_TRUE(tiny.overflowed());
  EXPECT_LT(strlen(small), sizeof(small));

  char big[64];
  JsonBuf json(big, sizeof(big));
  json.ch('{');
  json.keyStr("k", "a\"b");
  json.ch('}');
  EXPECT_FALSE(json.overflowed());
  EXPECT_STREQ(big, "{\"k\":\"a\\\"b\"}");
}

TEST(WordBank, HasDistinctCodeWordsThatFitATile) {
  const uint16_t count = words::codeWordCount();
  ASSERT_GE(count, 100);
  std::set<std::string> unique;
  for (uint16_t i = 0; i < count; i++) {
    const char* word = words::codeWord(i);
    EXPECT_LE(strlen(word), 9u) << word;
    EXPECT_GT(strlen(word), 0u);
    unique.insert(word);
  }
  EXPECT_EQ(unique.size(), count);
  ASSERT_GE(words::pairCount(), 20);
  for (uint16_t i = 0; i < words::pairCount(); i++) {
    EXPECT_STRNE(words::pairCivilian(i), words::pairUndercover(i));
  }
}

// --- lobby -------------------------------------------------------------------

TEST(GameSession, SeatsPlayersAndKeepsOneHost) {
  GameSession session(1);
  seat(session, 3);
  EXPECT_EQ(session.playerCount(), 3);
  EXPECT_TRUE(session.isHost(0));
  EXPECT_FALSE(session.isHost(1));

  // A reload carries the same token and must not burn a second seat.
  EXPECT_EQ(session.join("P1 again", 1u, 500), 0);
  EXPECT_EQ(session.playerCount(), 3);

  session.leave(0);
  EXPECT_EQ(session.playerCount(), 2);
  EXPECT_TRUE(session.isHost(1)) << "host moves to the lowest occupied seat";
}

TEST(GameSession, RejectsAnEmptyTableAndAFullOne) {
  GameSession session(2);
  EXPECT_EQ(session.join("nobody", 0, 0), -1) << "token 0 marks a free seat";
  seat(session, MAX_PLAYERS);
  EXPECT_EQ(session.join("late", 99u, 0), -1);
}

TEST(GameSession, VersionMovesWheneverTheScreenWouldChange) {
  GameSession session(3);
  const uint32_t start = session.version();
  seat(session, 3);
  EXPECT_GT(session.version(), start);

  const uint32_t beforePick = session.version();
  session.selectGame(GameId::Codenames);
  EXPECT_GT(session.version(), beforePick);
  session.selectGame(GameId::Codenames);
  EXPECT_EQ(session.version(), beforePick + 1) << "reselecting the same game is not a change";
}

TEST(GameSession, GatesStartOnTheMinimumSeatCount) {
  GameSession session(4);
  session.selectGame(GameId::Codenames);
  seat(session, 3);
  EXPECT_FALSE(session.canStart());
  EXPECT_FALSE(session.startGame());

  ASSERT_EQ(session.join("P4", 4u, 0), 3);
  EXPECT_TRUE(session.canStart());
  EXPECT_TRUE(session.startGame());
  EXPECT_FALSE(session.inLobby());
  EXPECT_EQ(session.game()->id(), GameId::Codenames);

  // A round in progress is closed to newcomers: roles are already dealt.
  EXPECT_EQ(session.join("P5", 5u, 0), -1);
}

TEST(GameSession, HostDrivesTheLobbyFromTheirPhone) {
  GameSession session(5);
  seat(session, 4);
  EXPECT_TRUE(session.applyAction(0, verb(Verb::Pick, gameIndex(GameId::Codenames))));
  EXPECT_EQ(session.selected(), GameId::Codenames);
  EXPECT_FALSE(session.applyAction(1, verb(Verb::Pick, gameIndex(GameId::Ludo)))) << "only the host picks";

  EXPECT_TRUE(session.applyAction(0, verb(Verb::Next)));
  EXPECT_FALSE(session.inLobby());
}

TEST(GameSession, CyclesThroughEveryGame) {
  GameSession session(6);
  std::set<int> seen;
  for (uint8_t i = 0; i < GAME_COUNT; i++) {
    seen.insert(static_cast<int>(session.selected()));
    session.cycleGame(1);
  }
  EXPECT_EQ(seen.size(), GAME_COUNT);
  const GameId before = session.selected();
  session.cycleGame(-1);
  session.cycleGame(1);
  EXPECT_EQ(session.selected(), before) << "cycling wraps in both directions";
}

TEST(GameSession, LobbyJsonNamesTheGamesAndThePlayers) {
  GameSession session(7);
  seat(session, 3);
  char buf[2048];
  const size_t written = session.writeStateJson(buf, sizeof(buf), 0, 0);
  ASSERT_GT(written, 0u);
  EXPECT_EQ(buf[written - 1], '}') << "a truncated document would not close";
  const std::string json(buf);
  EXPECT_NE(json.find("\"seat\":0,\"me\":0,"), std::string::npos) << "without `as`, me is the seat";
  EXPECT_NE(json.find("\"phase\":\"lobby\""), std::string::npos);
  EXPECT_NE(json.find("\"host\":true"), std::string::npos);
  EXPECT_NE(json.find("\"k\":\"battleship\""), std::string::npos);
  EXPECT_NE(json.find("\"n\":\"P2\""), std::string::npos);
}

TEST(GameSession, MarksAPhoneAwayWhenItStopsPolling) {
  GameSession session(8);
  ASSERT_EQ(session.join("P1", 1u, 1000), 0);
  EXPECT_FALSE(session.isAway(0, 1000));
  EXPECT_TRUE(session.isAway(0, 1000 + GameSession::AWAY_AFTER_MS + 1));
  session.touch(0, 40000);
  EXPECT_FALSE(session.isAway(0, 40000));
}

// --- the round loop and rejoining ---------------------------------------------

TEST(RoundLoop, OnlyTheHostEndsARoundAndOnlyWhenThereIsOne) {
  GameSession session(30);
  seat(session, 4);
  EXPECT_FALSE(session.applyAction(0, verb(Verb::End))) << "no round to end";

  ASSERT_TRUE(session.startGame());
  EXPECT_FALSE(session.applyAction(1, verb(Verb::End))) << "only the host ends it";
  EXPECT_FALSE(session.inLobby());
  EXPECT_TRUE(session.applyAction(0, verb(Verb::End)));
  EXPECT_TRUE(session.inLobby()) << "a running round goes back to the lobby";

  // A finished round: the impostor voted out.
  ASSERT_TRUE(session.startGame());
  auto* game = static_cast<Undercover*>(session.game());
  int8_t impostor = -1;
  for (uint8_t i = 0; i < 4; i++) {
    if (game->isUndercover(i)) impostor = static_cast<int8_t>(i);
  }
  ASSERT_GE(impostor, 0);
  ASSERT_TRUE(session.applyAction(0, verb(Verb::Next)));
  for (uint8_t i = 0; i < 4; i++) {
    const int16_t target = i == impostor ? (impostor == 0 ? 1 : 0) : impostor;
    session.applyAction(i, verb(Verb::Vote, target));
  }
  ASSERT_TRUE(session.game()->isOver());
  EXPECT_FALSE(session.applyAction(1, verb(Verb::End)));
  EXPECT_TRUE(session.applyAction(0, verb(Verb::End)));
  EXPECT_TRUE(session.inLobby()) << "and so does a finished one";
}

TEST(RoundLoop, AnAwayPhoneRejoinsItsSeatByNameMidRound) {
  GameSession session(31);
  session.selectGame(GameId::Battleship);
  ASSERT_EQ(session.join("Ann", 1u, 0), 0);
  ASSERT_EQ(session.join("Bob", 2u, 0), 1);
  ASSERT_TRUE(session.startGame());

  const uint32_t later = GameSession::AWAY_AFTER_MS + 1;
  session.touch(0, later);
  ASSERT_TRUE(session.isAway(1, later));
  const uint32_t before = session.version();
  EXPECT_EQ(session.join("bob", 77u, later, true), 1) << "case-insensitive, and the seat keeps its role";
  EXPECT_GT(session.version(), before);
  EXPECT_EQ(session.playerCount(), 2);
  EXPECT_EQ(session.seatForToken(77u), 1);
  EXPECT_EQ(session.seatForToken(2u), -1) << "the lost token no longer resolves";
  EXPECT_FALSE(session.isAway(1, later));
  EXPECT_TRUE(session.applyAction(1, verb(Verb::Ready))) << "Bob plays on from the new token";
}

TEST(RoundLoop, AnAwayPhoneRejoinsItsSeatInTheLobbyToo) {
  GameSession session(32);
  seat(session, 3);
  const uint32_t later = GameSession::AWAY_AFTER_MS * 2;
  EXPECT_EQ(session.join("P2", 50u, later, true), 1);
  EXPECT_EQ(session.playerCount(), 3);
  EXPECT_EQ(session.seatForToken(2u), -1);
  EXPECT_TRUE(session.isHost(0)) << "rejoining does not move the host";
}

TEST(RoundLoop, TheHostRejoiningKeepsTheHost) {
  GameSession session(36);
  seat(session, 3);
  ASSERT_TRUE(session.startGame());
  const uint32_t later = GameSession::AWAY_AFTER_MS * 2;
  EXPECT_EQ(session.join("P1", 50u, later, true), 0);
  EXPECT_TRUE(session.isHost(0));
  EXPECT_TRUE(session.applyAction(0, verb(Verb::End))) << "and the new token drives as host";
}

TEST(RoundLoop, AnAwaySeatAsksBeforeItIsTaken) {
  GameSession session(37);
  seat(session, 3);
  ASSERT_TRUE(session.startGame());
  const uint32_t later = GameSession::AWAY_AFTER_MS * 2;
  const uint32_t before = session.version();
  EXPECT_EQ(session.join("P2", 50u, later), GameSession::JOIN_CAN_REJOIN) << "a namesake needs the Rejoin tap";
  EXPECT_EQ(session.seatForToken(2u), 1) << "the seat keeps its token";
  EXPECT_EQ(session.seatForToken(50u), -1);
  EXPECT_EQ(session.version(), before);
}

TEST(RoundLoop, APresentSeatIsNeverTaken) {
  GameSession session(33);
  session.selectGame(GameId::Battleship);
  seat(session, 2);
  EXPECT_EQ(session.join("P2", 50u, 1000), GameSession::JOIN_NAME_TAKEN) << "P2 is still polling";
  EXPECT_EQ(session.join("p2", 51u, 1000, true), GameSession::JOIN_NAME_TAKEN) << "the flag does not force it";
  ASSERT_TRUE(session.startGame());
  EXPECT_EQ(session.join("P2", 52u, 1000, true), GameSession::JOIN_NAME_TAKEN) << "mid-round too";
  EXPECT_EQ(session.seatForToken(2u), 1);
  EXPECT_EQ(session.playerCount(), 2);
}

TEST(RoundLoop, ATestSeatsNameIsTaken) {
  GameSession session(34);
  session.setTestMode(true);
  ASSERT_EQ(session.join("Ann", 1u, 0), 0);
  ASSERT_EQ(session.addTestPlayer(), 1);
  const uint32_t later = GameSession::AWAY_AFTER_MS * 2;
  EXPECT_EQ(session.join("test 2", 50u, later, true), GameSession::JOIN_NAME_TAKEN);
  EXPECT_TRUE(session.isTestSeat(1));
  EXPECT_EQ(session.playerCount(), 2);
}

TEST(RoundLoop, ANewNameMidRoundWatches) {
  GameSession session(35);
  seat(session, 3);
  ASSERT_TRUE(session.startGame());
  EXPECT_EQ(session.join("Zed", 50u, 0), -1);
  EXPECT_EQ(session.playerCount(), 3);
}

TEST(RoundLoop, LeavingIsForTheLobby) {
  GameSession session(38);
  seat(session, 3);
  ASSERT_TRUE(session.startGame());
  EXPECT_FALSE(session.leave(1)) << "a dealt seat holds a role";
  EXPECT_EQ(session.playerCount(), 3);
  session.endGame();
  EXPECT_TRUE(session.leave(1));
  EXPECT_EQ(session.playerCount(), 2);
}

TEST(RoundLoop, AwayReturnsRepaintOnlyTheLobby) {
  GameSession session(39);
  seat(session, 3);
  const uint32_t later = GameSession::AWAY_AFTER_MS * 2;
  uint32_t before = session.version();
  session.touch(1, later);
  EXPECT_GT(session.version(), before) << "the lobby's seat list shows away";

  ASSERT_TRUE(session.startGame());
  before = session.version();
  session.touch(1, later * 2);
  EXPECT_EQ(session.version(), before) << "nothing on the round's screen does";
}

TEST(RoundLoop, AutoNamesNeverRepeatASeatedName) {
  GameSession session(40);
  ASSERT_EQ(session.join("Player 2", 1u, 0), 0);
  ASSERT_EQ(session.join("", 2u, 0), 1);
  EXPECT_STREQ(session.player(1).name, "Player 1") << "Player 2 is taken";
  ASSERT_EQ(session.join(nullptr, 3u, 0), 2);
  EXPECT_STREQ(session.player(2).name, "Player 3");
  std::set<std::string> names;
  for (uint8_t i = 0; i < 3; i++) names.insert(session.player(i).name);
  EXPECT_EQ(names.size(), 3u);
}

// --- test players ------------------------------------------------------------

TEST(TestPlayers, FillEveryGameToItsMinimumFromOnePhone) {
  for (uint8_t index = 0; index < GAME_COUNT; index++) {
    const GameMeta& meta = gameMeta(gameAt(index));
    GameSession session(200u + index);
    session.setTestMode(true);
    session.selectGame(meta.id);
    ASSERT_EQ(session.join("Solo", 1u, 0), 0);
    while (!session.canStart()) ASSERT_GE(session.addTestPlayer(), 0) << meta.key;
    EXPECT_EQ(session.playerCount(), meta.minPlayers) << meta.key;
    EXPECT_EQ(session.testPlayerCount(), meta.minPlayers - 1) << meta.key;
    EXPECT_TRUE(session.startGame()) << meta.key;
  }
}

TEST(TestPlayers, GetAFreshNameAndAPrivateToken) {
  GameSession session(201);
  session.setTestMode(true);
  ASSERT_EQ(session.join("Solo", 1u, 0), 0);
  ASSERT_EQ(session.addTestPlayer(), 1);
  ASSERT_EQ(session.addTestPlayer(), 2);
  EXPECT_STREQ(session.player(1).name, "Test 2");
  EXPECT_TRUE(session.isTestSeat(1));
  EXPECT_FALSE(session.isTestSeat(0));
  EXPECT_FALSE(session.isTestSeat(5)) << "an empty seat is not a test seat";

  const uint32_t token = session.player(1).token;
  EXPECT_NE(token, 0u);
  EXPECT_NE(token, session.player(0).token);
  EXPECT_NE(token, session.player(2).token);
  EXPECT_EQ(session.seatForToken(token), -1) << "no phone ever acts through a test seat's token";
}

TEST(TestPlayers, RefusedWhenTheTableIsFullOrARoundRuns) {
  GameSession session(202);
  session.setTestMode(true);
  ASSERT_EQ(session.join("Solo", 1u, 0), 0);
  for (uint8_t i = 1; i < MAX_PLAYERS; i++) ASSERT_EQ(session.addTestPlayer(), static_cast<int8_t>(i));
  const uint32_t full = session.version();
  EXPECT_EQ(session.addTestPlayer(), -1);
  EXPECT_EQ(session.version(), full) << "a refused seat is not a change";

  GameSession round(203);
  round.setTestMode(true);
  round.selectGame(GameId::Battleship);
  ASSERT_EQ(round.join("Solo", 1u, 0), 0);
  ASSERT_EQ(round.addTestPlayer(), 1);
  ASSERT_TRUE(round.startGame());
  EXPECT_EQ(round.addTestPlayer(), -1) << "roles are already dealt";
  EXPECT_FALSE(round.removeTestPlayer()) << "a dealt seat stays for the round";
  EXPECT_EQ(round.playerCount(), 2);
}

TEST(TestPlayers, RemoveDropsTheHighestTestSeatAndNeverAPhone) {
  GameSession session(204);
  session.setTestMode(true);
  ASSERT_EQ(session.join("Ann", 1u, 0), 0);
  ASSERT_EQ(session.addTestPlayer(), 1);
  ASSERT_EQ(session.addTestPlayer(), 2);
  ASSERT_EQ(session.join("Bob", 2u, 0), 3);

  EXPECT_TRUE(session.removeTestPlayer());
  EXPECT_FALSE(session.isSeated(2));
  EXPECT_TRUE(session.isSeated(3)) << "the highest seat is a phone, so it stays";
  EXPECT_TRUE(session.removeTestPlayer());
  EXPECT_FALSE(session.isSeated(1));
  EXPECT_FALSE(session.removeTestPlayer()) << "only phones are left";
  EXPECT_EQ(session.playerCount(), 2);
  EXPECT_EQ(session.testPlayerCount(), 0);
  EXPECT_TRUE(session.isSeated(0));
  EXPECT_TRUE(session.isSeated(3));
}

TEST(TestPlayers, StartingARoundPacksTheGapOutOfTheSeats) {
  GameSession session(210);
  session.setTestMode(true);
  session.selectGame(GameId::Battleship);
  ASSERT_EQ(session.join("Ann", 1u, 0), 0);
  ASSERT_EQ(session.addTestPlayer(), 1);
  ASSERT_EQ(session.addTestPlayer(), 2);
  ASSERT_EQ(session.join("Bob", 2u, 0), 3);
  ASSERT_TRUE(session.removeTestPlayer());
  ASSERT_TRUE(session.removeTestPlayer());

  ASSERT_TRUE(session.startGame());
  EXPECT_STREQ(session.player(1).name, "Bob") << "Battleship deals seats 0 and 1";
  EXPECT_EQ(session.seatForToken(2u), 1) << "the token follows its player";
  EXPECT_FALSE(session.isSeated(3));
  EXPECT_TRUE(session.isHost(0));
  EXPECT_TRUE(session.applyAction(1, verb(Verb::Ready))) << "Bob is dealt in";
}

TEST(TestPlayers, NamesStayDistinctAfterPacking) {
  GameSession session(212);
  session.setTestMode(true);
  session.selectGame(GameId::Battleship);
  ASSERT_EQ(session.join("Ann", 1u, 0), 0);
  ASSERT_EQ(session.join("Bob", 2u, 0), 1);
  ASSERT_EQ(session.addTestPlayer(), 2);
  ASSERT_STREQ(session.player(2).name, "Test 3");
  ASSERT_TRUE(session.leave(1));
  ASSERT_TRUE(session.startGame());
  ASSERT_STREQ(session.player(1).name, "Test 3") << "packed down from seat 2";
  session.endGame();

  ASSERT_EQ(session.addTestPlayer(), 2);
  EXPECT_STREQ(session.player(2).name, "Test 1") << "Test 3 is taken, so the lowest free number";
  std::set<std::string> names;
  for (uint8_t i = 0; i < MAX_PLAYERS; i++) {
    if (session.isSeated(i)) names.insert(session.player(i).name);
  }
  EXPECT_EQ(names.size(), static_cast<size_t>(session.playerCount()));
}

TEST(TestPlayers, PackingCarriesTheHostAlong) {
  // Up three times, a phone joins above them, Down once: seats {0, 1, 3}.
  GameSession session(211);
  session.setTestMode(true);
  ASSERT_EQ(session.addTestPlayer(), 0);
  ASSERT_EQ(session.addTestPlayer(), 1);
  ASSERT_EQ(session.addTestPlayer(), 2);
  ASSERT_EQ(session.join("Ann", 1u, 0), 3);
  ASSERT_TRUE(session.removeTestPlayer());
  ASSERT_TRUE(session.isHost(3));

  ASSERT_TRUE(session.startGame());
  EXPECT_EQ(session.seatForToken(1u), 2);
  EXPECT_TRUE(session.isHost(2));
  EXPECT_TRUE(session.isTestSeat(0));
  EXPECT_TRUE(session.isTestSeat(1));
  EXPECT_FALSE(session.isSeated(3));
  EXPECT_TRUE(session.applyAction(2, verb(Verb::Ready))) << "Ann is dealt in";
  EXPECT_TRUE(session.applyAction(2, verb(Verb::Next))) << "and still drives the round as host";
}

TEST(TestPlayers, APhoneJoiningTakesTheHostFromATestSeat) {
  GameSession session(205);
  session.setTestMode(true);
  ASSERT_EQ(session.addTestPlayer(), 0);
  EXPECT_TRUE(session.isHost(0)) << "alone at the table, a test seat hosts";
  ASSERT_EQ(session.addTestPlayer(), 1);
  ASSERT_EQ(session.join("Ann", 1u, 0), 2);
  EXPECT_TRUE(session.isHost(2));
  ASSERT_EQ(session.join("Bob", 2u, 0), 3);
  EXPECT_TRUE(session.isHost(2)) << "a second phone does not take a phone's host";
}

TEST(TestPlayers, RehostPrefersAPhone) {
  GameSession session(206);
  session.setTestMode(true);
  ASSERT_EQ(session.join("Ann", 1u, 0), 0);
  ASSERT_EQ(session.addTestPlayer(), 1);
  ASSERT_EQ(session.join("Bob", 2u, 0), 2);
  ASSERT_TRUE(session.leave(0));
  EXPECT_TRUE(session.isHost(2)) << "the lower test seat is passed over for a phone";
  ASSERT_TRUE(session.leave(2));
  EXPECT_TRUE(session.isHost(1)) << "with no phone left, the test seat keeps the table";
}

TEST(TestPlayers, AreNeverAway) {
  GameSession session(207);
  session.setTestMode(true);
  ASSERT_EQ(session.join("Ann", 1u, 0), 0);
  ASSERT_EQ(session.addTestPlayer(), 1);
  const uint32_t later = GameSession::AWAY_AFTER_MS * 10;
  EXPECT_TRUE(session.isAway(0, later));
  EXPECT_FALSE(session.isAway(1, later));
}

TEST(TestPlayers, TheHostSeatsAndDropsThemFromThePhone) {
  GameSession session(208);
  session.setTestMode(true);
  seat(session, 2);
  EXPECT_FALSE(session.applyAction(1, verb(Verb::AddTest))) << "only the host seats a test player";
  EXPECT_TRUE(session.applyAction(0, verb(Verb::AddTest)));
  EXPECT_TRUE(session.isTestSeat(2));
  EXPECT_FALSE(session.applyAction(1, verb(Verb::DropTest)));
  EXPECT_TRUE(session.applyAction(0, verb(Verb::DropTest)));
  EXPECT_EQ(session.testPlayerCount(), 0);
  EXPECT_FALSE(session.applyAction(0, verb(Verb::DropTest))) << "nothing left to drop";

  EXPECT_TRUE(session.applyAction(0, verb(Verb::AddTest)));
  ASSERT_TRUE(session.startGame());
  EXPECT_FALSE(session.applyAction(0, verb(Verb::AddTest))) << "not while a round runs";
  EXPECT_FALSE(session.applyAction(0, verb(Verb::DropTest)));
  EXPECT_EQ(session.playerCount(), 3);
}

TEST(TestPlayers, TheDocumentMarksThem) {
  GameSession session(209);
  session.setTestMode(true);
  ASSERT_EQ(session.join("Ann", 1u, 0), 0);
  ASSERT_EQ(session.addTestPlayer(), 1);
  char buf[2048];
  const size_t written = session.writeStateJson(buf, sizeof(buf), 0, 0);
  ASSERT_GT(written, 0u);
  const std::string json(buf);
  EXPECT_NE(json.find("{\"s\":0,\"n\":\"Ann\",\"host\":true,\"away\":false,\"test\":false}"), std::string::npos)
      << json;
  EXPECT_NE(json.find("{\"s\":1,\"n\":\"Test 2\",\"host\":false,\"away\":false,\"test\":true}"), std::string::npos)
      << json;

  // The host playing the test seat: its view, with "me" still naming the host.
  session.writeStateJson(buf, sizeof(buf), 1, 0, 0);
  EXPECT_NE(std::string(buf).find("\"seat\":1,\"me\":0,\"name\":\"Test 2\""), std::string::npos) << buf;
  session.writeStateJson(buf, sizeof(buf), -1, -1, 0);
  EXPECT_NE(std::string(buf).find("\"seat\":-1,\"me\":-1,"), std::string::npos) << buf;
}

TEST(TestPlayers, AreOffUnlessTheBuildTurnsThemOn) {
  GameSession session(213);
  seat(session, 2);
  EXPECT_FALSE(session.testMode()) << "production is the default";
  const uint32_t before = session.version();
  EXPECT_EQ(session.addTestPlayer(), -1);
  EXPECT_FALSE(session.applyAction(0, verb(Verb::AddTest))) << "the host's verb is a no-op";
  EXPECT_FALSE(session.removeTestPlayer());
  EXPECT_FALSE(session.applyAction(0, verb(Verb::DropTest)));
  EXPECT_EQ(session.playerCount(), 2);
  EXPECT_EQ(session.testPlayerCount(), 0);
  EXPECT_EQ(session.version(), before);

  char buf[2048];
  session.writeStateJson(buf, sizeof(buf), 0, 0);
  EXPECT_NE(std::string(buf).find("\"testMode\":false"), std::string::npos) << buf;
  session.setTestMode(true);
  session.writeStateJson(buf, sizeof(buf), 0, 0);
  EXPECT_NE(std::string(buf).find("\"testMode\":true"), std::string::npos) << buf;
  EXPECT_TRUE(session.applyAction(0, verb(Verb::AddTest)));
}

// --- undercover --------------------------------------------------------------

TEST(Undercover, DealsOneImpostorAndTwoInABigGroup) {
  for (uint8_t count = 3; count <= MAX_PLAYERS; count++) {
    Undercover game;
    Rng rng(count * 31u + 1u);
    game.start(count, rng);
    uint8_t impostors = 0;
    for (uint8_t i = 0; i < count; i++) {
      if (game.isUndercover(i)) impostors++;
    }
    EXPECT_EQ(impostors, count >= 6 ? 2 : 1) << "count " << static_cast<int>(count);
  }
}

TEST(Undercover, EveryoneReadyOpensTheVote) {
  Undercover game;
  Rng rng(11);
  game.start(4, rng);
  EXPECT_EQ(game.phase(), Undercover::Phase::Describe);
  for (uint8_t i = 0; i < 3; i++) {
    EXPECT_TRUE(game.apply(i, verb(Verb::Ready), false));
    EXPECT_EQ(game.phase(), Undercover::Phase::Describe);
  }
  EXPECT_FALSE(game.apply(0, verb(Verb::Ready), false)) << "ready twice is not a change";
  EXPECT_TRUE(game.apply(3, verb(Verb::Ready), false));
  EXPECT_EQ(game.phase(), Undercover::Phase::Vote);
}

TEST(Undercover, OnlyTheHostMovesThePhaseAlong) {
  Undercover game;
  Rng rng(15);
  game.start(4, rng);
  EXPECT_FALSE(game.apply(2, verb(Verb::Next), false)) << "a guest cannot cut the describing short";
  EXPECT_EQ(game.phase(), Undercover::Phase::Describe);
  EXPECT_TRUE(game.apply(2, verb(Verb::Next), true)) << "the device holds the host's authority";
  EXPECT_EQ(game.phase(), Undercover::Phase::Vote);
}

TEST(Undercover, EliminatesTheMostVotedAndRefusesATie) {
  Undercover game;
  Rng rng(12);
  game.start(4, rng);
  game.apply(0, verb(Verb::Next), true);  // host skips the describing phase
  ASSERT_EQ(game.phase(), Undercover::Phase::Vote);

  EXPECT_FALSE(game.apply(0, verb(Verb::Vote, 0), false)) << "no voting for yourself";
  EXPECT_FALSE(game.apply(0, verb(Verb::Vote, 9), false)) << "seat out of range";

  // 2-2 split: nobody leaves.
  game.apply(0, verb(Verb::Vote, 1), false);
  game.apply(1, verb(Verb::Vote, 0), false);
  game.apply(2, verb(Verb::Vote, 3), false);
  game.apply(3, verb(Verb::Vote, 2), false);
  EXPECT_EQ(game.phase(), Undercover::Phase::Reveal);
  EXPECT_EQ(game.lastEliminated(), -1);
  for (uint8_t i = 0; i < 4; i++) EXPECT_TRUE(game.alive(i));

  ASSERT_TRUE(game.apply(0, verb(Verb::Next), true));
  EXPECT_EQ(game.phase(), Undercover::Phase::Describe);
  EXPECT_EQ(game.roundNumber(), 2);

  game.apply(0, verb(Verb::Next), true);
  game.apply(0, verb(Verb::Vote, 2), false);
  game.apply(1, verb(Verb::Vote, 2), false);
  game.apply(2, verb(Verb::Vote, 0), false);
  game.apply(3, verb(Verb::Vote, 2), false);
  EXPECT_FALSE(game.alive(2));
  EXPECT_EQ(game.lastEliminated(), 2);
}

TEST(Undercover, CiviliansWinWhenTheImpostorIsOut) {
  Undercover game;
  Rng rng(13);
  game.start(4, rng);
  int8_t impostor = -1;
  for (uint8_t i = 0; i < 4; i++) {
    if (game.isUndercover(i)) impostor = static_cast<int8_t>(i);
  }
  ASSERT_GE(impostor, 0);

  game.apply(0, verb(Verb::Next), true);
  for (uint8_t i = 0; i < 4; i++) {
    const int16_t target = i == impostor ? (impostor == 0 ? 1 : 0) : impostor;
    game.apply(i, verb(Verb::Vote, target), false);
  }
  EXPECT_TRUE(game.isOver());
  EXPECT_EQ(game.winner(), 1);
}

TEST(Undercover, ShowsEachPhoneOnlyItsOwnWord) {
  Undercover game;
  Rng rng(14);
  game.start(4, rng);
  std::string impostorWord;
  std::string civilianWord;
  for (uint8_t i = 0; i < 4; i++) {
    char buf[1024];
    JsonBuf json(buf, sizeof(buf));
    game.writeView(json, static_cast<int8_t>(i));
    const std::string view(buf);
    EXPECT_EQ(view.find("\"role\":0"), std::string::npos) << "roles stay hidden while everyone is alive";
    EXPECT_EQ(view.find("\"role\":1"), std::string::npos);
    const size_t at = view.find("\"word\":\"");
    ASSERT_NE(at, std::string::npos);
    const std::string word = view.substr(at + 8, view.find('"', at + 8) - at - 8);
    (game.isUndercover(i) ? impostorWord : civilianWord) = word;
  }
  EXPECT_FALSE(impostorWord.empty());
  EXPECT_FALSE(civilianWord.empty());
  EXPECT_NE(impostorWord, civilianWord);
}

// --- codenames ---------------------------------------------------------------

TEST(Codenames, DealsTheStandardKey) {
  Codenames game;
  Rng rng(21);
  game.start(4, rng);

  uint8_t counts[4] = {0, 0, 0, 0};
  std::set<std::string> unique;
  for (uint8_t i = 0; i < Codenames::TILES; i++) {
    counts[game.owner(i)]++;
    unique.insert(game.tileWord(i));
    EXPECT_FALSE(game.revealed(i));
  }
  EXPECT_EQ(unique.size(), Codenames::TILES) << "no repeated word on the grid";
  EXPECT_EQ(counts[Codenames::ASSASSIN], 1);
  EXPECT_EQ(counts[Codenames::NEUTRAL], 7);
  EXPECT_EQ(counts[game.turn()], 9) << "the team that starts gets the extra agent";
  EXPECT_EQ(counts[game.turn() == Codenames::RED ? Codenames::BLUE : Codenames::RED], 8);
}

TEST(Codenames, GivesEachTeamOneSpymaster) {
  for (uint8_t count = 4; count <= MAX_PLAYERS; count++) {
    Codenames game;
    Rng rng(count * 7u);
    game.start(count, rng);
    uint8_t spies[3] = {0, 0, 0};
    for (uint8_t i = 0; i < count; i++) {
      if (game.isSpymaster(i)) spies[game.team(i)]++;
    }
    EXPECT_EQ(spies[Codenames::RED], 1) << "count " << static_cast<int>(count);
    EXPECT_EQ(spies[Codenames::BLUE], 1) << "count " << static_cast<int>(count);
  }
}

namespace {

// Finds a seat on `team` that is (or is not) the spymaster.
int8_t findSeat(const Codenames& game, const uint8_t count, const uint8_t team, const bool spymaster) {
  for (uint8_t i = 0; i < count; i++) {
    if (game.team(i) == team && game.isSpymaster(i) == spymaster) return static_cast<int8_t>(i);
  }
  return -1;
}

int8_t findTile(const Codenames& game, const uint8_t owner) {
  for (uint8_t i = 0; i < Codenames::TILES; i++) {
    if (game.owner(i) == owner && !game.revealed(i)) return static_cast<int8_t>(i);
  }
  return -1;
}

}  // namespace

TEST(Codenames, OnlyTheSpymasterOnTurnMayClue) {
  Codenames game;
  Rng rng(22);
  game.start(4, rng);
  const uint8_t on = game.turn();
  const uint8_t off = on == Codenames::RED ? Codenames::BLUE : Codenames::RED;

  EXPECT_FALSE(game.apply(findSeat(game, 4, off, true), verb(Verb::Clue, 2, -1, -1, "OCEAN"), false));
  EXPECT_FALSE(game.apply(findSeat(game, 4, on, false), verb(Verb::Clue, 2, -1, -1, "OCEAN"), false));
  EXPECT_FALSE(game.apply(findSeat(game, 4, on, true), verb(Verb::Clue, 2, -1, -1, nullptr), false))
      << "a clue needs a word";
  EXPECT_TRUE(game.apply(findSeat(game, 4, on, true), verb(Verb::Clue, 2, -1, -1, "OCEAN"), false));
  EXPECT_EQ(game.phase(), Codenames::Phase::Guess);
  EXPECT_STREQ(game.clue(), "OCEAN");
  EXPECT_EQ(game.guessesLeft(), 3) << "the clue count plus the traditional bonus guess";
  EXPECT_FALSE(game.apply(findSeat(game, 4, on, true), verb(Verb::Guess, 0), false)) << "spymasters never guess";
}

TEST(Codenames, AWrongColourEndsTheTurn) {
  Codenames game;
  Rng rng(23);
  game.start(4, rng);
  const uint8_t on = game.turn();
  const uint8_t off = on == Codenames::RED ? Codenames::BLUE : Codenames::RED;
  game.apply(findSeat(game, 4, on, true), verb(Verb::Clue, 2, -1, -1, "OCEAN"), false);

  const int8_t ownTile = findTile(game, on);
  ASSERT_TRUE(game.apply(findSeat(game, 4, on, false), verb(Verb::Guess, ownTile), false));
  EXPECT_TRUE(game.revealed(ownTile));
  EXPECT_EQ(game.turn(), on) << "a correct guess keeps the turn";

  const int8_t theirTile = findTile(game, off);
  ASSERT_TRUE(game.apply(findSeat(game, 4, on, false), verb(Verb::Guess, theirTile), false));
  EXPECT_EQ(game.turn(), off);
  EXPECT_EQ(game.phase(), Codenames::Phase::Clue);
  EXPECT_FALSE(game.apply(findSeat(game, 4, on, false), verb(Verb::Guess, findTile(game, on)), false))
      << "the turn really did pass";
}

TEST(Codenames, TheAssassinEndsItImmediately) {
  Codenames game;
  Rng rng(24);
  game.start(4, rng);
  const uint8_t on = game.turn();
  const uint8_t off = on == Codenames::RED ? Codenames::BLUE : Codenames::RED;
  game.apply(findSeat(game, 4, on, true), verb(Verb::Clue, 1, -1, -1, "DANGER"), false);
  ASSERT_TRUE(game.apply(findSeat(game, 4, on, false), verb(Verb::Guess, findTile(game, Codenames::ASSASSIN)), false));
  EXPECT_TRUE(game.isOver());
  EXPECT_EQ(game.winner(), off);
}

TEST(Codenames, FindingEveryAgentWins) {
  Codenames game;
  Rng rng(25);
  game.start(4, rng);
  const uint8_t on = game.turn();
  const int8_t spy = findSeat(game, 4, on, true);
  const int8_t field = findSeat(game, 4, on, false);

  for (int guard = 0; guard < 30 && !game.isOver(); guard++) {
    if (game.phase() == Codenames::Phase::Clue) {
      ASSERT_EQ(game.turn(), on) << "the other team never gets a turn in this run";
      ASSERT_TRUE(game.apply(spy, verb(Verb::Clue, 9, -1, -1, "ALL"), false));
    }
    ASSERT_TRUE(game.apply(field, verb(Verb::Guess, findTile(game, on)), false));
  }
  EXPECT_TRUE(game.isOver());
  EXPECT_EQ(game.winner(), on);
  EXPECT_EQ(game.remaining(on), 0);
}

TEST(Codenames, ZeroAndUnlimitedCluesLiftTheGuessLimit) {
  for (const int16_t count : {int16_t{0}, int16_t{Codenames::UNLIMITED}}) {
    Codenames game;
    Rng rng(27);
    game.start(4, rng);
    const uint8_t on = game.turn();
    const int8_t field = findSeat(game, 4, on, false);
    ASSERT_TRUE(game.apply(findSeat(game, 4, on, true), verb(Verb::Clue, count, -1, -1, "COLD"), false));
    EXPECT_TRUE(game.unlimited());
    for (int guess = 0; guess < 3; guess++) {
      ASSERT_TRUE(game.apply(field, verb(Verb::Guess, findTile(game, on)), false));
    }
    EXPECT_EQ(game.turn(), on) << "three right past a clue of " << count << ", still guessing";
    EXPECT_EQ(game.phase(), Codenames::Phase::Guess);
  }

  Codenames game;
  Rng rng(27);
  game.start(4, rng);
  const int8_t spy = findSeat(game, 4, game.turn(), true);
  EXPECT_FALSE(game.apply(spy, verb(Verb::Clue, Codenames::UNLIMITED + 1, -1, -1, "COLD"), false));
}

TEST(Codenames, TheTeamGuessesOnceBeforeStopping) {
  Codenames game;
  Rng rng(28);
  game.start(4, rng);
  const uint8_t on = game.turn();
  const int8_t field = findSeat(game, 4, on, false);
  game.apply(findSeat(game, 4, on, true), verb(Verb::Clue, 2, -1, -1, "OCEAN"), false);

  EXPECT_FALSE(game.apply(field, verb(Verb::Pass), false)) << "no stopping before the first guess";
  ASSERT_TRUE(game.apply(field, verb(Verb::Guess, findTile(game, on)), false));
  EXPECT_EQ(game.guessesMade(), 1);
  EXPECT_TRUE(game.apply(field, verb(Verb::Pass), false));
  EXPECT_NE(game.turn(), on);
  EXPECT_EQ(game.guessesMade(), 0) << "the count starts over with the next team";
}

TEST(Codenames, AWordOnTheBoardIsNoClue) {
  Codenames game;
  Rng rng(29);
  game.start(4, rng);
  const uint8_t on = game.turn();
  const int8_t spy = findSeat(game, 4, on, true);
  const int8_t tile = findTile(game, on);
  const std::string word = game.tileWord(tile);
  std::string lower = word;
  for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

  EXPECT_FALSE(game.apply(spy, verb(Verb::Clue, 1, -1, -1, word.c_str()), false));
  EXPECT_FALSE(game.apply(spy, verb(Verb::Clue, 1, -1, -1, lower.c_str()), false)) << "case does not matter";
  EXPECT_FALSE(game.apply(spy, verb(Verb::Clue, 1, -1, -1, (lower + "s").c_str()), false)) << "nor a plural";
  EXPECT_EQ(game.phase(), Codenames::Phase::Clue);

  // Once the tile is covered its word is fair game.
  ASSERT_TRUE(game.apply(spy, verb(Verb::Clue, 1, -1, -1, "OCEAN"), false));
  ASSERT_TRUE(game.apply(findSeat(game, 4, on, false), verb(Verb::Guess, tile), false));
  game.apply(findSeat(game, 4, on, false), verb(Verb::Pass), false);
  const uint8_t next = game.turn();
  game.apply(findSeat(game, 4, next, true), verb(Verb::Clue, 1, -1, -1, "OCEAN"), false);
  game.apply(findSeat(game, 4, next, false), verb(Verb::Guess, findTile(game, next)), false);
  game.apply(findSeat(game, 4, next, false), verb(Verb::Pass), false);
  ASSERT_EQ(game.turn(), on);
  EXPECT_TRUE(game.apply(spy, verb(Verb::Clue, 1, -1, -1, word.c_str()), false));
}

TEST(Codenames, TheKeyIsOnlyOnTheSpymastersPhone) {
  Codenames game;
  Rng rng(26);
  game.start(4, rng);
  const int8_t spy = findSeat(game, 4, game.turn(), true);
  const int8_t field = findSeat(game, 4, game.turn(), false);

  char spyBuf[3072];
  JsonBuf spyJson(spyBuf, sizeof(spyBuf));
  game.writeView(spyJson, spy);
  EXPECT_FALSE(spyJson.overflowed());
  EXPECT_EQ(std::string(spyBuf).find("\"k\":-1"), std::string::npos) << "the spymaster sees every key";

  char fieldBuf[3072];
  JsonBuf fieldJson(fieldBuf, sizeof(fieldBuf));
  game.writeView(fieldJson, field);
  EXPECT_FALSE(fieldJson.overflowed());
  const std::string view(fieldBuf);
  size_t hidden = 0;
  for (size_t at = view.find("\"k\":-1"); at != std::string::npos; at = view.find("\"k\":-1", at + 1)) hidden++;
  EXPECT_EQ(hidden, Codenames::TILES) << "operatives see no key at all before a tile turns";
}

// --- battleship --------------------------------------------------------------

TEST(Battleship, RefusesOverlappingAndOverhangingShips) {
  Battleship game;
  Rng rng(31);
  game.start(2, rng);

  EXPECT_TRUE(game.apply(0, verb(Verb::Place, 0, 0, 0, "h"), false));
  EXPECT_FALSE(game.apply(0, verb(Verb::Place, 1, 0, 0, "h"), false)) << "overlaps the carrier";
  EXPECT_TRUE(game.apply(0, verb(Verb::Place, 1, 0, 1, "h"), false));
  EXPECT_FALSE(game.apply(0, verb(Verb::Place, 2, 8, 2, "h"), false)) << "hangs off the right edge";
  EXPECT_FALSE(game.apply(0, verb(Verb::Place, 2, 2, 8, "v"), false)) << "hangs off the bottom edge";
  EXPECT_TRUE(game.apply(0, verb(Verb::Place, 0, 4, 4, "v"), false)) << "a ship may be moved while placing";
}

TEST(Battleship, ReadyFillsTheFleetAndStartsTheDuel) {
  Battleship game;
  Rng rng(32);
  game.start(2, rng);
  EXPECT_TRUE(game.apply(0, verb(Verb::Ready), false));
  EXPECT_EQ(game.phase(), Battleship::Phase::Place) << "still waiting for the second captain";
  EXPECT_FALSE(game.apply(0, verb(Verb::Fire, 0, 0), false));
  EXPECT_TRUE(game.apply(1, verb(Verb::Ready), false));
  EXPECT_EQ(game.phase(), Battleship::Phase::Fire);
  EXPECT_EQ(game.shipsLeft(0), Battleship::SHIPS);
  EXPECT_EQ(game.shipsLeft(1), Battleship::SHIPS);
}

TEST(Battleship, OneShotPerTurnAndNoRepeats) {
  Battleship game;
  Rng rng(33);
  game.start(2, rng);
  game.apply(0, verb(Verb::Ready), false);
  game.apply(1, verb(Verb::Ready), false);

  EXPECT_FALSE(game.apply(1, verb(Verb::Fire, 0, 0), false)) << "seat 1 opens";
  ASSERT_TRUE(game.apply(0, verb(Verb::Fire, 3, 4), false));
  EXPECT_EQ(game.turn(), 1);
  EXPECT_NE(game.shotAt(1, 3, 4), Battleship::UNKNOWN);
  ASSERT_TRUE(game.apply(1, verb(Verb::Fire, 3, 4), false));
  EXPECT_FALSE(game.apply(0, verb(Verb::Fire, 3, 4), false)) << "that square is already spent";
}

TEST(Battleship, SinkingEveryShipWins) {
  Battleship game;
  Rng rng(34);
  game.start(2, rng);
  game.apply(0, verb(Verb::Ready), false);
  game.apply(1, verb(Verb::Ready), false);

  // Seat 1 works the grid; seat 2 fires into a corner it has already used once,
  // which is rejected, so only seat 1 makes progress.
  uint8_t cell = 0;
  for (int guard = 0; guard < 400 && !game.isOver(); guard++) {
    if (game.turn() == 0) {
      const uint8_t x = cell % Battleship::SIZE;
      const uint8_t y = cell / Battleship::SIZE;
      cell++;
      game.apply(0, verb(Verb::Fire, x, y), false);
    } else {
      // Seat 2 keeps missing on purpose by shooting the same square twice; the
      // second attempt is refused, so force a fresh square each time.
      const uint8_t x = (cell * 7) % Battleship::SIZE;
      const uint8_t y = (cell * 3) % Battleship::SIZE;
      if (!game.apply(1, verb(Verb::Fire, x, y), false)) {
        for (uint8_t i = 0; i < Battleship::CELLS; i++) {
          if (game.shotAt(0, i % Battleship::SIZE, i / Battleship::SIZE) == Battleship::UNKNOWN) {
            ASSERT_TRUE(game.apply(1, verb(Verb::Fire, i % Battleship::SIZE, i / Battleship::SIZE), false));
            break;
          }
        }
      }
    }
  }
  EXPECT_TRUE(game.isOver());
  EXPECT_GE(game.winner(), 0);
  EXPECT_EQ(game.shipsLeft(1 - game.winner()), 0);
}

TEST(Battleship, APhoneNeverSeesTheOtherFleet) {
  Battleship game;
  Rng rng(35);
  game.start(2, rng);
  game.apply(0, verb(Verb::Ready), false);
  game.apply(1, verb(Verb::Ready), false);

  char buf[2048];
  JsonBuf json(buf, sizeof(buf));
  game.writeView(json, 0);
  EXPECT_FALSE(json.overflowed());
  const std::string view(buf);
  const size_t fleetAt = view.find("\"fleet\":\"");
  ASSERT_NE(fleetAt, std::string::npos);
  const std::string fleet = view.substr(fleetAt + 9, Battleship::CELLS);
  uint8_t occupied = 0;
  for (const char c : fleet) {
    if (c != '0') occupied++;
  }
  EXPECT_EQ(occupied, 5 + 4 + 3 + 3 + 2) << "own ships are there";

  const size_t outAt = view.find("\"outgoing\":\"");
  ASSERT_NE(outAt, std::string::npos);
  const std::string outgoing = view.substr(outAt + 12, Battleship::CELLS);
  EXPECT_EQ(outgoing, std::string(Battleship::CELLS, '0')) << "and the enemy grid is blank until fired on";
}

// --- the served document -----------------------------------------------------

TEST(GameSession, EveryGameViewFitsTheServerBuffer) {
  // GameServer serves from one 3 KB buffer; Codenames with a full table is the
  // largest document, so it decides the size.
  constexpr size_t SERVER_BUFFER = 3072;
  for (uint8_t index = 0; index < GAME_COUNT; index++) {
    GameSession session(100u + index);
    seat(session, MAX_PLAYERS);
    session.selectGame(gameAt(index));
    ASSERT_TRUE(session.startGame()) << gameMeta(gameAt(index)).key;

    for (int8_t viewer = -1; viewer < static_cast<int8_t>(MAX_PLAYERS); viewer++) {
      char buf[SERVER_BUFFER];
      const size_t written = session.writeStateJson(buf, sizeof(buf), viewer, 1000);
      EXPECT_LT(written, SERVER_BUFFER - 64) << gameMeta(gameAt(index)).key << " leaves no headroom";
      EXPECT_EQ(buf[written - 1], '}') << gameMeta(gameAt(index)).key << " document was truncated";
    }
  }
}

TEST(GameSession, HostCanDealAgainOrReturnToTheLobby) {
  GameSession session(55);
  seat(session, 4);
  session.selectGame(GameId::Codenames);
  ASSERT_TRUE(session.startGame());

  // Finish a round the short way: walk the guessing team into the assassin.
  const auto finishRound = [&session] {
    auto* game = static_cast<Codenames*>(session.game());
    const uint8_t on = game->turn();
    int8_t spy = -1;
    int8_t field = -1;
    for (uint8_t i = 0; i < 4; i++) {
      if (game->team(i) != on) continue;
      (game->isSpymaster(i) ? spy : field) = static_cast<int8_t>(i);
    }
    ASSERT_TRUE(session.applyAction(spy, verb(Verb::Clue, 1, -1, -1, "X")));
    for (uint8_t i = 0; i < Codenames::TILES; i++) {
      if (game->owner(i) != Codenames::ASSASSIN) continue;
      ASSERT_TRUE(session.applyAction(field, verb(Verb::Guess, i)));
      break;
    }
    ASSERT_TRUE(session.game()->isOver());
  };

  finishRound();
  const uint8_t host = static_cast<uint8_t>(session.hostSeat());
  const uint8_t nonHost = host == 0 ? 1 : 0;
  EXPECT_FALSE(session.applyAction(nonHost, verb(Verb::Next))) << "only the host restarts";
  EXPECT_TRUE(session.applyAction(host, verb(Verb::Next)));
  EXPECT_FALSE(session.inLobby());
  EXPECT_FALSE(session.game()->isOver()) << "a fresh deal of the same game";

  EXPECT_FALSE(session.applyAction(host, verb(Verb::Pick, gameIndex(GameId::Ludo))))
      << "a live round is not abandoned by a tap on a phone";

  finishRound();
  EXPECT_TRUE(session.applyAction(host, verb(Verb::Ready)));
  EXPECT_TRUE(session.inLobby()) << "the host can walk a finished round back to the lobby";
  EXPECT_TRUE(session.applyAction(host, verb(Verb::Pick, gameIndex(GameId::Ludo))));
  EXPECT_EQ(session.selected(), GameId::Ludo);
}
