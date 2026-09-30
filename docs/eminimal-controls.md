# e-Minimal five-button controls

e-Minimal has five physical buttons: four front keys in a row under the
screen — Back, Select (Confirm), Up, Down, left to right as the hint bar
draws them — plus Power. No side buttons, no Left/Right, no touch.
`five_button::active()`
(`src/util/FiveButtonInput.h`) is the runtime gate every screen below checks:
true whenever the board has no touch, is `DigitalButtons`, and has Left or
Right unassigned. Upstream boards with a real Left/Right keep their own grammar.
Everything below names the logical roles; which physical key plays each role
is the user's key map (next section).

## Key map and orientation
- **Remap Front Buttons** (Settings > Controls) assigns the four front keys
  to Back, Select, Up, Down. The wizard asks "Press the button for …" for each
  role in turn; a key already taken is refused. Keys assign on release. After
  the fourth, a summary shows the new layout and waits: pressing the new
  Select key (hinted "Save") saves; nothing is written before that. Hold Back
  (the current Back) 1 s at any point to cancel; hold Select (the current
  Select) 1 s to reset to the default layout and exit. Power is not a role;
  its hold still opens the power menu. Stored as `buttonMapBack`,
  `buttonMapConfirm`, `buttonMapUp`, `buttonMapDown` (HalGPIO::BTN_* indices)
  in settings.json; anything but a permutation of the four keys resets to
  the default (`CrossPointSettings::validateButtonMap()`).
- **Page Turn Buttons** picks the reader direction: "Up = back, Down =
  forward" (default) or "Down = back, Up = forward". Same `sideButtonLayout`
  field and indices as upstream's Side Button Layout; its other values
  (Disabled, Next/Next, Prev/Prev) aren't offered here.
- **Orient Front Buttons** (off by default) turns Up/Down with the device when
  the reader is rotated: swapped in Inverted and Landscape CCW (the Down key
  comes first on screen there), unchanged in Portrait and Landscape CW. The
  hint bar labels swap with them. Table: `MappedInputManager::upDownKey()`.
- Stays physical whatever the map: the boot-time Up+Power recovery hold and
  the Power+Down screenshot chord. The boot-time Back hold (skip resuming the
  book) follows the map.

## Home
Up/Down walk one flat list: current book, then the grid, then the bottom
tabs, wrapping at both ends. Hold Down jumps to the tabs, hold Up to the
first book. Select opens the highlighted item; Back resumes the most
recently read book directly (when one exists).

## Library / Settings ring
Up/Down move the tab band; Select drops into that tab's rows, which loop;
Back climbs back out to the tab band. Hold Select opens that tab's own
options (e.g. sort order) only on the tab band; on a row it opens
recent-book options, delete, or collapse instead, where offered
(`LibraryListActivity.cpp:727-738`).

## Reader
Up/Down turn pages (direction: Page Turn Buttons); hold either to skip a
chapter, when enabled in Settings (Long-press behaviour). With Orient Front
Buttons on, a rotated page keeps "up" on screen as Up (see Key map above).
Select opens the reader menu (forced to List style
here — Toolbar needs touch). Hold Select drops/clears a bookmark, when
enabled in Settings (Long-press menu function) — both are OFF by default
(`longPressButtonBehavior = OFF`, `longPressMenuFunction = LP_MENU_DISABLED`
in `CrossPointSettings.h`). Back short
goes Home; held past `GO_BACK_OR_HOME_MS` it opens the file browser instead
(or the reverse, if `backShortToFileBrowser` is set).

## Keyboard (on-device)
A linear walk over the key grid, not Left/Right + Up/Down cursor motion:
Up/Down step through keys row-major, wrapping row-to-row and end-to-end.
Select types the highlighted key; hold Select submits. Back deletes a
character (cancels on an empty field); hold Back always cancels.

## Text entry chooser (reader or phone)
Two rows: type on this reader, or on a phone via a QR code. Select picks
one; hold Select remembers it as the default (Settings > Controls > Text
Entry) and skips the chooser next time.

## Go to % / Time to sleep
Up/Down press for ±1 (±1 minute); hold either to repeat at the dialog's
large step: Go to % steps by 10, Time to Sleep by 5 (`SettingsActivity.cpp:492`).
Hints read "Press Up/Down: 1" / "Hold Up/Down: <large step>"; the Up slot
shows "+", Down shows "-".

## Dictionary word select
A tap of Up/Down steps one word at a time. Holding either (≥500 ms) jumps a
line instead, repeating every 350 ms; the release ending a line-jump hold
does not also step a word. Select looks up the word; Back returns to the reader.

## Wi-Fi list
The network list gains a trailing "Scan again" row (Select rescans), since
there is no Right button to rescan with directly. Hold Select on a saved
network forgets it. The save/forget prompts hint Up/Down in place of
Left/Right to move between Yes/No.

## OPDS browser
Up/Down move the row selection; Select opens or downloads the entry; hold
Select opens search when the feed offers one (no synthetic row, which would
shift every entry's index). Back climbs the feed hierarchy.

## Power
Hold Power ~0.7 s while awake for the power menu. Hold Power ~1 s while
asleep to wake. A short tap does nothing either way — no double-click
frontlight shortcut here.

The power menu opens on Sleep, so hold Power then Select still sleeps. Above
the rows (Sleep, Go Home, Refresh Screen) sit three app tiles: **Reader**,
**Dashboard**, **Games**. Up/Down walk one ring through the tiles and then the
rows. The app that is open has a heavier frame and says "Open"; Select on it
just closes the menu, Select on another switches to it. Reader resumes the book
that was on screen when the reader was left, else opens Home; Games opens the
games list, which keeps the radio off until a game is picked. Go Home is the reader's Home, so it also switches to Reader.

## Apps and boot
When the app last opened (`lastApp` in `/.crosspoint/state.json`) is the
reader, the device boots and wakes into it as before: the book it slept in, or
Home. After the Dashboard or Games it shows **Apps** instead: the power menu's
three tiles with Sleep beneath, the cursor on the app last used, so Select
reopens it and Up/Down pick another. Back does nothing there. Hold Back through
boot or wake to land on Home whichever app it was. Game Night reboots when it
is left (to shed the WiFi heap), so switching away from Games shows the loading
popup first; that reboot goes straight to the app chosen, not to Apps.

## Games
**Picker.** A 2x2 grid of picture tiles. Up/Down move through the tiles in
reading order, Select opens the game's lobby and starts the hotspot. Back does
nothing (the app's root; the power menu switches apps).

**Lobby.** Select starts the game. Up adds a test player and Down removes the
highest-numbered one (test builds only; a test player is a seat with no phone,
for trying a game alone). The hints read "+ Test" and "- Test" and follow the
key remap and Orient Front Buttons. Test players can be added or removed only in
the lobby. Back leaves Game Night: when anyone is seated it asks "Leave Game
Night? Every phone drops off." (Stay / Leave Game Night), else it leaves
directly.

**Round.** Select is the host's "next". Back opens a menu: Resume the round,
End the round - back to the lobby, Leave Game Night. Up/Down move, Select picks,
Back closes it and resumes.

**Round over.** Select deals the same game again; Back goes to the lobby.

See `docs/party-games.md`.

## Dashboard
The home page is portrait like the other apps; only the launched dashboard
turns. It is the app's root (Back does nothing; the power menu switches apps)
and has four rows:

- **Launch Dashboard**: the active layout's widgets in their tiles.
- **Change Layout**: a thumbnail of every layout and a New Layout card, each
  drawn in the dashboard's shape with the widget in every tile. Select puts
  one in use; hold Select (or New Layout) opens the web interface on that
  layout's editor.
- **Widgets**: Weather, To-do, Image and Date with whether each is set up and
  on the layout in use. Select opens the web interface on that widget.
- **Orientation**: Landscape CW (keys on the left), Landscape CCW (keys on the
  right) or Portrait (keys at the bottom), shown as pictures; also Settings >
  Display > Dashboard Orientation.

Everything else is set in the web interface: the Dashboards page of the same
web UI as the books (`/dashboards`), in three tabs. **Layouts**: a strip of
thumbnails (a tap puts one in use), New / Edit / Delete, and the layout editor
(name; place, move and resize widgets on a 14 x 10 grid, 10 x 14 for portrait;
up to twelve layouts of eight tiles). Every layout is editable and deletable,
including the three the app starts with (**Wide top**, **Grid**, **Picture
top**); with none left the dashboard asks for one. **Widgets**: the weather
place (searched by name, or coordinates) and units, the image folder and
interval with **Add pictures** (converted in the browser, then uploaded or
saved to copy onto the card), the to-do list. **Settings**: orientation and
**Restore starting layouts**. A layout drawn for the other shape
is shown turned on its side. The device's QR screens (Change Layout, Widgets)
first ask, as File Transfer does, whether to **Join a Network** (the usual
WiFi picker) or **Create Hotspot** (the shared `eMinimal` network), remembering
the choice, then open that page at the right section; leaving once the radio
was up restarts the device (the loading popup) back into the Dashboard.

Every phone portal (file transfer's hotspot, phone typing, WiFi setup, the
Dashboard) raises the same `eMinimal` network with one saved passphrase, so a
phone keeps a single network. Game Night keeps its own for now.

**Launched**, the tiles fill the screen: the grid's inner edges sit on the
panel's partial-refresh blocks (128-px cells, whole 32-px columns and 16-row
bands of panel memory, in every orientation) and the outer tiles stretch to the
viewable edge. Each widget lays itself out by its tile's shape (square, wide,
tall) and size (small, medium, large). Up/Down move the heavy frame between tiles; Select opens the
framed one full screen; Back there returns to the tiles, and Back on the tiles
to the home page. No key hints are drawn when landscape (they would run
sideways along the key edge).

**Weather.** The tile shows now (icon, temperature, condition, high/low) and,
when wide, the next days; else feels-like, rain and wind. Opened: the full
forecast with humidity and sunrise/sunset and the next four days; Select
fetches now. Data comes from Open-Meteo over a saved WiFi network (join one
once from File Transfer): on entry when the cache is stale, then daily at about
05:00 local time, retrying hourly up to three times after a failure. The last
forecast is cached (`/.crosspoint/weather.json`). A fetch also sets the
system clock when nothing else has (to within ~8 minutes).

**To-do.** The tile shows the open count and the first open tasks. Opened:
the list (`/.crosspoint/todo.json`, up to 50 tasks of 80 characters), also
managed on the web page. The first row adds a task through the usual text
entry (reader keyboard or phone). Select on a task ticks it done or not done;
hold Select on a task for Mark Done / Not Done, Edit Task, Delete and Clear
Done Tasks.

**Image.** The JPEG, PNG and BMP files of the chosen folder (default `/carousel`),
one per interval (default 30 min), in name order: cropped to fill the tile and
dithered black and white there, the whole picture in grey when opened. Camera
photos up to about 50 MP decode (at a reduced scale first; a big one takes a
few seconds). Opened, Down and Up step forward and back and
restart the interval. The position is kept in `state.json`.

**Date.** Weekday, day and month, rolling over at midnight. It needs the
system clock, which the first weather fetch sets (the board has no clock chip).

**Standby.** The launched dashboard keeps the device awake as a display: 30 s
after it is launched, a key or any wake, it hides the focus frame and
light-sleeps until the next widget update is due or any key is pressed; the
panel keeps showing it. A widget opened full screen does the same: Weather and
Image wake for their own updates too, To-do only for a key. The key that wakes it only wakes and brings the frame
back; a timed wake shows it too for the next 30 s. Power menu Sleep is the
normal sleep with the sleep screen; the device then wakes into Apps. Leaving
the Dashboard app after the radio was used (a weather fetch, a place search)
restarts the device on the way (the loading popup), like the other WiFi
screens.

## SD card folders
At every boot the reader makes the standard folders that are missing, so a
fresh card needs no setup: `/Books` and `/Manga` (suggested homes; the Library
finds books anywhere), `/sleep` (sleep-screen pictures), `/carousel` or the
folder chosen for the Dashboard's Image widget, `/fonts`, `/dictionaries` and
`/screenshots`. Existing folders and files are left alone.

## Hidden on this board, and why
Settings hides Menu Style in the reader section (forced to List; Toolbar
needs Left/Right or touch). Upstream's Side Button Layout shows as Page Turn
Buttons with its two directions only — Disabled, Next/Next and Prev/Prev
leave no key turning one way. Display hides UI Theme: e-Minimal ships its
own theme only (`FREEINK_DEVICE_EMINIMAL`; `fromJson()` pins `uiTheme` to
`EMINIMAL`). Home Button settings stay gated on `hasHomeKey()`, which this
board lacks.

## Divergence from the vault
The Obsidian vault's 04 §2 button table predates this pass and differs (Back
short = "advance ring", Select long = "full refresh"). What is actually
built: Back climbs rather than advancing a ring, and Refresh lives in the
power menu, not on a held Select. Treat this file as current until the vault
is amended.
