# Party games

Game Night turns the reader into the shared board for a table of people. The
device raises an open access point and shows the same join card as File
Transfer: one QR joins the Wi-Fi, one opens the page. Every phone that joins
gets a private view of the same session — a dice cup, a secret word, a spymaster's key — while the e-ink
panel shows the one thing everybody is allowed to see.

Nothing leaves the room: there is no internet leg, no account and no server
beyond the reader itself.

## Starting a game night

1. Open **Games** from the power menu (or **File Transfer → Game Night**). It
   shows the four games as a 2x2 grid of picture tiles: a drawn thumbnail per
   game, its seat count along the tile's bottom edge, the title under the tile,
   and the selected tile ringed like the Home cover grid. The selected game's
   description sits under the grid. The radio stays off here.
2. Pick a game. The reader starts the `eMinimal Games` access point through
   `PhonePortal` (open, up to eight stations, captive DNS, mDNS `eminimal`) and
   shows the lobby: the `PhoneJoinPanel` card above the seat list.
3. Players scan the left QR to join the network. Most phones then open the page
   by themselves through the captive portal; the right QR (and
   `http://192.168.4.1/` or `http://eminimal.local/`) is there for the ones that
   do not.
4. The first player to join is the host. The host starts the game from their
   phone or with the device's Select, and can still switch games from the phone.

### Device controls

| Screen | Button | Action |
|--------|--------|--------|
| Games | Up / Down, Confirm | Move through the tiles; Confirm opens the game's lobby and starts the hotspot |
| Games | Back | Nothing (the app's root; the power menu switches apps) |
| Lobby | Confirm | Start the selected game |
| Lobby | Up | Add a test player (test builds only) |
| Lobby | Down | Remove the last test player (test builds only) |
| Lobby | Back | Leave Game Night: asks first ("Leave Game Night? Every phone drops off." - Stay / Leave Game Night) when anyone is seated, else leaves directly; the device reboots into the games list to shed the WiFi heap |
| Round | Confirm | The host's "next": push the phase along |
| Round | Back | Opens the menu: Resume the round, End the round - back to the lobby, Leave Game Night |
| Menu | Up / Down, Select, Back | Move, pick, close (Back resumes the round) |
| Round over | Confirm | Deal the same game again |
| Round over | Back | Back to the lobby |

The device acts with the host's authority, so a round never stalls because the
host's phone is in someone's pocket.

The phone page plays short synthesized sounds. A speaker button in the top bar
toggles them; the choice is remembered on that phone.

On the phone, "Leave the table" asks before it gives the seat up.

### Game loop

Lobby → start → play → round over → deal again or back to the lobby, for as
many rounds as the table wants.

- **Start.** The host's phone or the device's Select, once the game's seat
  minimum is at the table.
- **Round over.** The board stays up with "Round over - Next deals again". Select
  (or the host's phone) deals the same game again; Back (or the host's phone)
  returns to the lobby to pick another.
- **Ending a round early.** On the device, Back opens a small menu over the
  board: Resume the round, End the round - back to the lobby, or Leave Game
  Night. The phones keep playing while it is open, and Back closes it, so one
  stray press never throws a round away. The host's phone has "End the round",
  with a confirm, while a round is running or over (the `end` verb).
- **Leaving Game Night.** From the menu, or Back in the lobby, which asks first
  whenever anyone is seated ("Leave Game Night? Every phone drops off."). The
  access point goes down and the reader reboots into the games list.

### Rejoining

A phone keeps its seat through a reload: its token lives in `localStorage`. A
phone that loses the token instead (browser back, a captive-portal sheet closed,
a different browser) takes the seat back on purpose, never by accident:

- Once the old phone has missed about 10 s of polls, the seat counts as away.
- The page lists away seats as "Rejoin as <name>" buttons. Typing an away
  seat's name into the join box instead answers "<name> is away - tap Rejoin if
  that is you", and the page offers the same Rejoin confirm.
- Rejoining (`join` with `rejoin=1`) hands the seat the new token, in the lobby
  or mid-round, with its role, hand, turn and host status intact. The old token
  stops working.
- A name whose seat is still polling, or that a test player holds, is taken
  ("pick another name"), so two people can never share a seat. A new name
  mid-round watches as a spectator until the next lobby.

Leaving (`leave`) is for the lobby only: mid-round a seat holds a role, so the
server keeps it and answers with the seat's unchanged view.

### Testing alone

A test player is a seat with no phone behind it, named "Test N" (numbered as
they are added). It lets one person with one phone start any game and play every
seat; Codenames needs four, so join, then press Up three times.

- **Adding and removing.** Only in the lobby, never mid-round. On the device, Up
  adds one and Down removes the highest-numbered; the hints read "+ Test" and
  "- Test" and follow the key remap and Orient Front Buttons. A help line under
  the seat list reads "Testing alone? + Test seats a test player; play its turns
  from your phone", and test seats show "(test)". On the phone the host has "Add a
  test player" and "Remove one" buttons.
- **Hosting.** A test player is never shown as away. A phone that joins takes
  over as host from a test player, and when the host leaves, hosting passes to a
  phone before a test player.
- **Playing their turns.** While test players are at the table, the host's
  phone gets a "Playing as" switcher: its own name, then each test player.
  Picking a test player makes the phone show that seat's private view and act
  as that seat. No other phone gets it, since that view holds the seat's
  secrets. The choice is not remembered across a reload.

Test mode is the build flag `GAMES_TEST_MODE`, on in the `eminimal` env for
bench testing. Deleting its line from `platformio.ini` builds a production image
with no test players: no lobby key, hint or help line; no test card or switcher
on the phone; and the server refuses `addtest`/`droptest` and ignores `as`. The
state document's `testMode` says which build a phone is talking to.

## The games

| Game | Seats | Plays | The phone holds |
|------|-------|-------|-----------------|
| Undercover | 3–8 | everyone | your secret word, and your vote |
| Codenames | 4–8 | everyone | the colour key, for the two spymasters |
| Battleship | 2 | first two seats | your fleet and your shots |
| Ludo | 2–4 | first four seats | the dice and your tokens |

Extra players in the lobby are spectators for the two-seat and four-seat games:
they still see the board on the shared screen. When a round starts, the seats
are packed down to 0..N-1 in order, so a seat freed in the lobby is never dealt.

**Undercover.** Everyone gets the same word except the undercover, who gets a
close neighbour of it (two undercovers from six players up). Players describe
their word out loud, then vote on their phones. Civilians win when the last
undercover is out; the undercover wins on reaching parity. A tied vote
eliminates nobody.

**Codenames.** Standard rules: 25 words, 9 agents for the team that starts,
8 for the other, 7 bystanders, one assassin. The spymaster's clue buys one more
guess than the number given; a clue of 0 or unlimited (∞ on the phone) lets the
team guess until it misses. The team must guess once before it may stop, a
wrong colour ends the turn, and the assassin ends the game. The reader refuses a
clue that is a word still on the board, or its plural (BANK, BANKS); subtler
calls are left to the other spymaster. On a monochrome panel the teams are a fill pattern rather than a
colour, so the board carries a legend: Red is solid, Blue is hatched, a
bystander is struck through, the assassin is crossed out. The phone board uses
the same fill patterns. An operative taps a word to pick it (tap it again to
drop it, or tap another to move the pick) and presses Guess to send it; a pick
stays on that phone until then. In the view, `clueCount` is -1 for an unlimited
clue, `guessesLeft` is -1 while there is no limit, and `guessed` counts this
turn's guesses.

**Battleship.** Carrier, battleship, cruiser, submarine, destroyer on a 10×10
grid. On your phone, tap a ship, then the square it starts from; Across and Down
turn it. Or press Ready and the rest drop in at random. One shot per turn, hit or miss. The shared screen shows both grids with
the ships hidden — only hits, misses and sunk hulls.

**Ludo.** Each player is a shape — circle, square, triangle or diamond — picked
on the phone before the first roll. The shape decides the corner: circle top
left, then square, triangle and diamond clockwise, each with its own start
square and home column. A player can switch to another free shape until the
last one is taken; the device's Select deals the free shapes to anyone who has
not picked (lowest shape first, in seat order) and starts the dice. Turns go
round the board by shape: the circle, or the lowest shape held, rolls first.
Tokens carry their number, 1 to 4, inside the owner's shape on the board and on
the phone. Then the usual rules: four tokens each, a six to leave the yard, an
exact roll to reach home, capture on an unsafe square, an extra turn for a six,
a capture or a token coming home, and three sixes in a row forfeits the turn.
On the board each yard's top row names its owner beside a house holding their
count of tokens home; the row is inverted for the player on turn (the winner,
once the game is over). The view adds `shapes` (per dealt seat, -1 until picked) and the phase `pick`.

## Architecture

| Piece | Where | Depends on |
|-------|-------|------------|
| Rules, seats, JSON views | `lib/PartyGames/` | nothing — plain C++ |
| HTTP surface | `src/network/GameServer.cpp` | Arduino `WebServer` |
| Phone page | `src/network/html/PlayPage.html` | `/css/app.css`, baked into flash |
| Screen, access point, lifecycle | `src/activities/games/GameNightActivity.cpp` | the activity stack, `PhonePortal`, `PhoneJoinPanel` |
| Board pixels, picker thumbnails (`drawThumbnail`) | `src/activities/games/GameBoards.cpp` | `GfxRenderer` |

The split is the point. `lib/PartyGames` has no Arduino and no renderer in it,
so the rules run under `test/party_games` on a host and a rule change is
provable without a device. Everything that needs pixels or a socket lives on the
device side of the line.

`GameServer` is deliberately not `CrossPointWebServer`: the file manager,
WebDAV and the WebSocket upload path have no business in front of a room full of
guests, and they cost heap that the access point and the session need.

## Endpoints

Served on port 80 while Game Night is running. Anything that is neither
`/api/*` nor `/css/app.css` redirects to `/`, which is what makes the captive
portal pop the page open.

| Method | Path | Purpose |
|--------|------|---------|
| `GET` | `/` | The phone page (gzipped, ETagged) |
| `GET` | `/css/app.css` | The shared web UI stylesheet, the one the file-transfer pages use (gzipped, ETagged) |
| `GET` | `/api/game/state?t=<token>` | The caller's view of the session |
| `POST` | `/api/game/join?name=<name>[&rejoin=1]` | Take a seat, or with `rejoin=1` reclaim an away one by name; returns a token |
| `POST` | `/api/game/act?t=<token>&v=<verb>&a=&b=&c=&s=` | Play |
| `POST` | `/api/game/leave?t=<token>` | Give the seat back (lobby only) |

`t` is a 32-bit hex token from `esp_random()`, kept in the phone's
`localStorage` so a reload does not consume a second seat. The token is a seat
key, not a credential: the network is open, and the threat model is a room of
people you invited.

`join` answers with one of:

| Reply | When |
|-------|------|
| `{"t": "…", "seat": n, "rejoined": false}` | A new seat (lobby only) |
| `{"t": "…", "seat": n, "rejoined": true}` | `rejoin=1` and the name's seat was away: reclaimed (see Rejoining) |
| `{"error": "<name> is away - tap Rejoin if that is you", "rejoin": "<name>"}` | The name's phone seat is away, and `rejoin=1` was not sent |
| `{"error": "<name> is already at the table - pick another name"}` | The name's seat is still polling, or is a test player |
| `{"error": "A round is on - rejoin under your name, or watch"}` | A new name mid-round |
| `{"error": "No free seat right now"}` | The table is full |

Names match without regard to case, and an unnamed phone gets the first free
"Player N".

Every action replies with the state it produced, so a tap does not wait out the
poll interval. Phones poll `state` about once a second; a poll costs one
serialization into a single fixed buffer, no allocation.

`state` and `act` take an optional `as=<seat>` (digits only). When the caller's
token holds the host's seat and `<seat>` is a test player, the request is
answered or applied as that seat; any other `as` is ignored. `leave` ignores
`as`.

Verbs: `ready`, `next`, `clue` (Codenames: `s` = the word, `a` = 0-9, or 10
for unlimited), `guess`, `pass`, `place`, `fire`, `vote`, `roll`, `move`,
`shape` (Ludo, before the first roll: `a` = shape 0-3), plus
`pick` (host, lobby or a finished round: `a` = game index), `addtest` and
`droptest` (host, lobby only) and `end` (host, while a round is running or
over: back to the lobby). `pick` and `shape` are separate so a stale shape tap
can never switch the game. The session refuses anything that is not that
seat's to do, so an edited URL gets a 200 and no change.

### State document

```json
{
  "v": 42, "seat": 2, "me": 2, "name": "Ann", "host": false,
  "game": "codenames", "title": "Codenames", "phase": "play",
  "canStart": true, "testMode": true, "minPlayers": 4, "maxPlayers": 8,
  "status": "Red: OCEAN 2 - 3 guesses left",
  "players": [{"s": 0, "n": "Ann", "host": true, "away": false, "test": false}],
  "over": false,
  "g": { "...": "the game's own view, private parts included" }
}
```

`v` is the session version. It moves only when something changed, which is what
the shared screen repaints on — a phone polling eight times a second never costs
a panel refresh.

`me` is the caller's own seat (-1 when its token is not seated). It differs from
`seat` while the host plays as a test player.

In the lobby the document carries a `games` array instead of `g`, so the host's
phone can offer the picker without knowing what this firmware was built with.

## Costs

- **Flash**: about 5 KB for the phone page (gzipped), 5 KB of word lists, and
  the code. Measure a build before and after if it matters on a 16 MB part.
  `GameServer` carries its own copy of the gzipped stylesheet (about 4.4 KB),
  as each other server that includes `appCss.generated.h` does.
- **DRAM**: the session is 640 bytes of in-place game storage plus eight seats;
  the server holds one 3 KB JSON buffer. The active game is built in place with
  placement new rather than on the heap, because a party churns through rounds
  and a repeated new/delete of a ~500 byte object is exactly the fragmentation
  a reader with WiFi up cannot afford.
- **Panel**: repaints are rate-limited to one per 700 ms and promoted to a full
  refresh every eighth paint to clear the ghosting that fast refreshes leave.

## Adding a game

1. Subclass `party::Game` in `lib/PartyGames/`. Plain data members only: no
   heap, no Arduino, no `std::string`.
2. Add it to `GameId`, to the `kGames` table in `GameSession.cpp`, and to the
   `startGame()` switch. The `static_assert` there will tell you if it outgrew
   the in-place storage.
3. Write `writeView()` so a phone sees its own secrets and nobody else's. This
   is the part worth testing hardest.
4. Add a board painter in `GameBoards.cpp` and a case to `drawBoard()` and
   `statusFor()`.
5. Add a panel to `PlayPage.html`.
6. Draw its thumbnail in `GameBoards.cpp` `drawThumbnail()` and mirror it in
   `PlayPage.html`'s thumbnail SVG.
7. Add a title string to `lib/I18n/translations/english.yaml` and a case to
   `titleFor()`.
8. Test the rules in `test/party_games/PartyGamesTest.cpp`:

   ```bash
   cmake -S test -B build/test
   cmake --build build/test --target PartyGamesTest
   ./build/test/party_games/PartyGamesTest
   ```

## Not done yet

- **Access point only.** There is no "join my home network" mode; the reader
  always hosts. On a phone that means losing internet for the duration.
- **No touch input on the board.** Everything on the device is driven by the
  physical buttons, even on touch boards.
- **Board text is English.** The device's chrome and status line go through
  `tr()`; the phone page and the words on the tiles do not, the same way the
  rest of the web UI does not.
- **No text adventure.** It wants a story format on the SD card rather than
  another compiled-in rule set, which is a different piece of work.
