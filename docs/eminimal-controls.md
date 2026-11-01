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
- **Display Orientation** (Global Settings > Display & System; the web
  Settings page lists it under Display) turns the whole device: Home, menus,
  settings, the Dashboard's menus and the reader. It is the reader's
  `orientation` field, so the reader's own orientation tile moves it too, and
  it no longer appears in the reader's Settings. Sleep images stay portrait;
  the Dashboard keeps its own Dashboard Orientation. The panel sits upside
  down in the case; the renderer turns every frame 180 degrees for it
  (`GfxRenderer::getPanelOrientation()`), so the names stay true to the keys.
- **Orient Front Buttons** (off by default) turns Up/Down with the device when
  it is rotated: swapped in Inverted and Landscape CCW (the Down key
  comes first on screen there), unchanged in Portrait and Landscape CW. The
  hint bar labels swap with them. Table: `MappedInputManager::upDownKey()`.
- Stays physical whatever the map: the boot-time Up+Power recovery hold and
  the Power+Down screenshot chord. The boot-time Back hold (skip resuming the
  book) follows the map.

## Home
Up/Down walk one flat list: current book, then the grid, then the bottom
tabs, wrapping at both ends. Hold Down jumps to the tabs, hold Up to the
first book. Select opens the highlighted item; Back resumes the most
recently read book directly (when one exists). Hold Select (about 0.7 s) on a
book opens **Manage book**: its title, author, series, genre and file, with
Edit on phone (a file-transfer QR that opens the web Edit book dialog) and
Clear edited details (asks first; shown only while the book has overrides).
Up/Down move, Select picks, Back leaves.

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
here — Toolbar needs touch). Hold Select (about 0.4 s) runs
Settings > Controls > Hold Select Action while Hold Select Menu is On, which is
the default; Off makes a held Select do nothing in the reader. Hold Select
Action defaults to Ask, which opens a popup with Highlight, Dictionary and
Bookmark; the other values act directly. Both rows are five-button only. A
settings.json without `holdSelectAction` takes it from the upstream
`longPressMenuFunction` once (Bookmark and Dictionary carry over, Disabled turns
Hold Select Menu off) and is resaved.
Chapter skip by hold (Long-press behaviour) is OFF by default. Back short
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

## Word picker (dictionary and highlights)
The picker works in two stages over the words on the current page.
- **Pick line.** Up/Down move between text rows (hints: Pick line, Prev line,
  Next line). The picker opens on a row near the middle of the page. Select
  enters the row; Back leaves the picker.
- **Pick word.** Opens on the row's longest word. Up/Down move along the row;
  Select picks the word; Back returns to pick line.

Dictionary lookups skip words with fewer than three letters or digits. The
highlight picker stops on every word. Picking a word in the dictionary opens
the lookup (Look Up on the hint bar); in highlights it sets the start or end.

## Manage Dictionaries
Settings > Reader > Manage Dictionaries replaces the Dictionary picker on this
board. One row per dictionary (folder name; "In use" marks the default, and the
hidden `/.dictionaries` root is shown), then an **Add Dictionary** row. Up/Down
move; Select makes the dictionary the default; hold Select (about 0.7 s) opens
Use as default, Rename, Delete and Add More. Rename asks through the usual text
entry (31 characters at most; refused when empty, invalid or already taken) and
keeps the folder in its root; Delete asks first and clears the default if it was
that one. **Add Dictionary** and Add More open the Dictionaries web page the same
way File Transfer does (Join a Network or Create Hotspot, then a QR code); leaving
after the radio was on restarts the device into Settings. With no dictionaries
the screen says so and Select (Add) opens the same page.

## Definition screen
Up/Down turn the definition's pages. Select returns to the reader. Back returns
to word selection.

## Highlights
Hold Select opens the picker to make a highlight (Hold Select Action =
Highlight, or Ask > Highlight). It runs in two steps on the page on screen:
- **Start here.** Pick the first word.
- **End here.** Pick the last word. A preview band covers the words from the
  start word to the cursor, in reading order.

Select on the end word saves the highlight. Back on the end step returns to the
start step; Back on the start step cancels. Picking the same word twice makes a
one-word highlight. If the start word is already highlighted, a popup offers
Remove highlight, or Highlight to start another one there. A highlight stays on
its page, since both ends are picked there. Highlighted words sit on a
light-grey band behind the text.

The reader menu's Highlights list shows each highlight's snippet with its
chapter title. Select jumps to the highlight; hold Select (about 0.7 s) deletes
it.

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
Hold Power ~0.7 s while awake for the power menu (headed **Quick Menu**). It
opens with a clearing refresh over any app. Hold Power ~1 s while
asleep to wake. A short tap does nothing either way — no double-click
frontlight shortcut here.

The power menu opens on Sleep, so hold Power then Select still sleeps. Above
the rows (Sleep, Go Home, Refresh Screen) sit three app tiles: **Reader**,
**Dashboard**, **Games**. Up/Down walk one ring through the tiles and then the
rows. The app that is open has a heavier frame and says "Open"; Select on it
just closes the menu, Select on another switches to it. Reader resumes the book
that was on screen when the reader was left, else opens Home; Games opens the
games list, which keeps the radio off until a game is picked. Go Home is the reader's Home, so it also switches to Reader.

## Global Settings > Power & Sleep
Two sub-pages. **Charge Badge** (On/Off; Style: bolt + %, bolt only or % only;
Size: Large 416, Medium 320 or Small 224 px; Position: centre or one of four
corners; Inverted) draws the charging badge over the sleep screen while USB is
in. When it is off there is no USB-detect wake and no 30-minute tick. **Sleep
Screen** holds the sleep screen style, cover mode, cover filter and quick resume.

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
highest-numbered one (a test player is a seat with no phone, for trying a game
alone; the first Up arms test mode for the session). The hints read "+ Test",
and "- Test" while there is one to drop, and follow the key remap and Orient
Front Buttons. Test players can be added or removed only in
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
