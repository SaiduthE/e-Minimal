# The web UI — design contract

The pages the reader serves to a phone or laptop: Home, Files, Settings,
Dashboards, Pictures, Fonts, the Wi-Fi setup page, and the phone text-entry page
(`TextEntryPage.html`).
This is the contract every page is built against so they read as one
product. Anything not covered here follows the reference implementation in
`src/network/html/HomePage.html`.

## Subject and job

An e-ink reader, **e-Minimal**, with five buttons (Up, Down, Select, Back,
Power) and no keyboard. These pages exist because a phone has the keyboard
and the file picker the device lacks.
The primary job is *get a book onto the card from a phone in under a minute*;
the secondary jobs are changing reading settings and adding fonts. Most visits
happen on a phone held in one hand, some on a laptop, occasionally inside a
captive-portal sheet (iOS CNA / Android sign-in webview) with no internet.

## Direction: the panel, extended

The phone page looks like the reader's own screen continued: ink on a pale,
neutral grey-white, no colour, no depth. E-ink has sixteen greys and no
shadows, so the UI has greys and rules, never drop shadows or gradients. The
one bold element is the **navigation drawer / sidebar, rendered inverted —
white on black — like the device's selected row.** Everything else is quiet.

### Tokens (`:root`, dark under `prefers-color-scheme: dark` — the panel inverted)

| token | light | dark | use |
|---|---|---|---|
| `--paper` | `#f2f2ef` | `#000000` | page background |
| `--surface` | `#ffffff` | `#141414` | cards, inputs |
| `--ink` | `#000000` | `#f2f2ef` | text, primary buttons, rules of emphasis |
| `--ink-2` | `#555555` | `#b0b0b0` | secondary text, labels |
| `--ink-3` | `#8a8a8a` | `#7a7a7a` | placeholders, disabled, hints |
| `--rule` | `#d6d6d2` | `#2a2a2a` | hairline borders, dividers |
| `--rule-2` | `#eaeae6` | `#1c1c1c` | row separators inside lists/tables |
| `--danger` | `#b3261e` | `#ff6b62` | destructive actions and error text only |
| `--ok` | `#1b6e3a` | `#5cc48a` | success text only |
| `--focus` | `#000000` | `#f2f2ef` | 2 px focus ring, offset 2 px |

No other colours. State is carried by weight, inversion and the two functional
colours. Never a tinted near-black: black is `#000`.

### Type

- **Serif for titles**: `"Iowan Old Style", Charter, "Noto Serif", Georgia, serif`
  — the device sets books in a serif. Page title (`h1`) and card titles (`h2`).
- **Sans for everything else**: `system-ui, -apple-system, "Segoe UI", Roboto, "Noto Sans", sans-serif`.
- Scale: base `16px`; `h1` 1.5rem/1.2 weight 600; `h2` 1.125rem/1.3 weight 600;
  body 1rem/1.5; small 0.875rem; tiny 0.8125rem (only for meta).
- Sentence case throughout. No all-caps labels, no tracked-out eyebrows, no
  emoji in headings or buttons, no `→` on links.
- Line length ≤ 72ch for running text.

### Shape and space

- Radius: `4px` on controls and cards, `999px` only on chips/toggles. One radius, not several.
- Borders are 1 px `--rule`. Emphasis is a 2 px `--ink` rule, never a shadow.
- Spacing scale: 4 / 8 / 12 / 16 / 24 / 32 px. Page gutter 16 px on phones, 32 px from 900 px.
- Motion only in answer to a tap (drawer open/close 180 ms, modal 120 ms), and
  none under `prefers-reduced-motion`.
- Tap targets ≥ 44 px tall on phones.

## The shell

`/js/app.js` injects the shell into `<body>` at load; the page ships only its
`<main>`. `/css/app.css` styles both. Both are served by the device with an
ETag and cached.

```
phone (< 900 px)                       laptop (≥ 900 px)
┌────────────────────────────┐         ┌──────────┬───────────────────────────┐
│ ☰  Files              ▮▮▮  │ topbar  │ e-Minimal│ Files               ▮▮▮   │
│────────────────────────────│         │          │───────────────────────────│
│ <main> 16 px gutters       │         │ Home     │ <main> max-width 880 px   │
│                            │         │ Files    │                           │
│                            │         │ Settings │                           │
│                            │         │ Fonts    │                           │
│                            │         │          │                           │
│                            │         │ eMinimal │                           │
│                            │         │ 192.168… │                           │
└────────────────────────────┘         └──────────┴───────────────────────────┘
 drawer: off-canvas from the left,       sidebar: 232 px, persistent, black.
 black, backdrop, closes on tap/Esc.
```

Markup app.js produces (ids and classes are the contract):

```html
<header class="topbar">
  <button class="menu-btn" id="menuBtn" aria-controls="drawer" aria-expanded="false" aria-label="Menu">…</button>
  <div class="topbar-title" id="topbarTitle">Files</div>
  <div class="topbar-status" id="topbarStatus"></div>   <!-- link chip: mode + IP -->
</header>
<div class="backdrop" id="backdrop"></div>
<nav class="drawer" id="drawer" aria-label="Pages">
  <a class="brand" href="/">e-Minimal</a>
  <a href="/" data-page="home">Home</a>
  <a href="/files" data-page="files">Files</a>
  <a href="/settings" data-page="settings">Settings</a>
  <a href="/dashboards" data-page="dashboards">Dashboards</a>
  <a href="/fonts" data-page="fonts">Fonts</a>
  <div class="drawer-foot" id="drawerFoot"></div>   <!-- hostname, IP, free memory, version -->
</nav>
```

- The page declares itself with `<body data-page="files" data-title="Files">`;
  app.js sets the active link and the topbar title from those. A page with no
  drawer link of its own names its parent with `data-nav` (Pictures has
  `data-nav="dashboards"`), which lights that link with `aria-current="true"`.
- app.js fetches `/api/status` once and fills `#topbarStatus` ("Joined · 192.168.50.240"
  or "Hotspot · 192.168.4.1") and `#drawerFoot`. Failure leaves them empty; no error UI.
- Drawer state: `body.drawer-open`. Opened by `#menuBtn`, closed by backdrop,
  Esc, or any drawer link. Focus returns to the button on close.
- The brand link in the topbar is omitted on phones (the title is enough); the
  drawer carries it.
- app.js also exposes `window.ui = { toast(text, kind), confirm(text, {ok, danger}) → Promise<bool> }`
  so pages stop hand-rolling message divs. `toast` shows for 3 s at the bottom;
  kinds: `""`, `"ok"`, `"danger"`.

## Components (app.css)

Class names are the contract; pages use these and keep their own CSS to what is
genuinely page-specific, scoped under `body[data-page="…"]`.

| class | what |
|---|---|
| `.card` | surface, 1 px rule, radius 4, padding 16 (24 from 900 px); `> h2` is the card title |
| `.card-head` | flex row: title left, actions right, wraps on phones |
| `.kv` / `.kv > div` | key–value rows (Home status): label `--ink-2`, value right-aligned, hairline between rows |
| `.btn` | 40 px tall (44 on touch), 1 px `--ink` border, transparent; `.btn-primary` inverted (ink fill, paper text); `.btn-danger` `--danger` border and text, inverted on hover; `.btn-ghost` no border; `.btn-sm` 32 px |
| `.field` | label above control, 8 px gap; `.field-hint` small `--ink-2` below |
| `input, select, textarea` | 40 px tall, surface fill, 1 px rule, focus = 2 px `--ink` ring; `select` gets an inline-SVG chevron; `input[type="range"]` stays the native slider (`appearance: auto`, `--ink` accent, 32 px, no box) so its track shows |
| `.toggle` | switch: 44 × 24 track, `--rule` off, `--ink` on, knob `--surface` |
| `.list` / `.list > .row` | vertical list of rows separated by `--rule-2`; row = flex, 44 px min, `.row-main` grows, `.row-meta` small `--ink-2` |
| `.table` | responsive table: from 700 px a real table with `th` in `--ink-2` small; below 700 px each row becomes a stacked block and cells show `data-label` as their key |
| `.toolbar` | flex row of buttons, 8 px gap, wraps; sticky under the topbar when `.toolbar-sticky` |
| `.modal-backdrop` / `.modal` | centred sheet on laptop, bottom sheet on phones (full width, radius top 8); `.modal-head`, `.modal-body`, `.modal-foot` (buttons right-aligned, primary last) |
| `.toast` | bottom-centred pill, inverted; `.toast-ok` / `.toast-danger` sit on `--surface` with `--ok` / `--danger` text and border (declared after `.toast`, which they override) |
| `.chip` | small inline status (`.chip-ok`, `.chip-danger`) |
| `.empty` | empty state: one sentence of what to do next, centred, `--ink-2` |
| `.savebar` | sticky bottom bar (surface, top rule) with a summary left and `.btn-primary` right; hidden until there is something to save |
| `.section-nav` | Settings: on ≥ 900 px a sticky list of section links left of the content (`.section-nav a.active` = 2 px ink rule left); below 900 px a `select` labelled "Section" that scrolls to the section |
| `.progress` | 4 px track `--rule`, fill `--ink` |
| `.visually-hidden` | the usual |

Dark mode: tokens only; no component overrides.

## Rules for every page

1. **Ship only `<main>`** plus `<link rel="stylesheet" href="/css/app.css">` and
   `<script src="/js/app.js" defer>` (plus the page's own script). `<body data-page data-title>`.
2. **Keep every element id, every JS function and every API call exactly as it
   is.** The C++ server is the other half of these pages; endpoints, methods,
   query strings and JSON shapes do not change. The Files page's EPUB optimiser,
   WebSocket upload and retry logic stay byte-for-byte in behaviour.
3. **Copy**: product name "e-Minimal"; sentence case; buttons say what happens
   ("Upload", "Save changes", "Delete 3 files"); empty states say what to do
   ("No files yet. Upload a book to start."); errors say what went wrong and what
   to do. No "CrossPoint" anywhere the user can see; no emoji.
4. **Responsive**: 360 px wide with no horizontal scroll, 768 px, 1280 px. Phone
   first. Tables stack below 700 px. Modals become bottom sheets.
5. **Accessible floor**: visible focus, labels on every control, `aria-expanded`
   on disclosures, dialogs trap focus and close on Esc, `prefers-reduced-motion`
   honoured, contrast ≥ 4.5:1 (the palette guarantees it if used as specified).
6. **Budget**: the four pages together stay under the size they are today
   (FilesPage 231 KB raw is dominated by its script; do not grow it). Vanilla
   JS, no frameworks, no external fonts or CDNs — the captive portal has no
   internet. One deliberate exception: Convert serves pdf.js from flash
   (`pdf.min.js` 89 KB + `pdf.worker.min.js` 290 KB gzipped), fetched only when
   a PDF is added — there is no CDN to fall back on. Dashboards' place search
   calls Open-Meteo's geocoding API from the browser; it is optional, and the
   page says so and points to the coordinate fields when there is no internet.
7. **Verify in a browser** before finishing: `python -m http.server 8000` from
   `src/network/html/` serves `/css/app.css` and `/js/app.js` at the right
   paths; open `http://localhost:8000/<Page>.html` at 360, 768 and 1280 px
   widths. API calls will fail there — the page must still lay out and show its
   empty/error states cleanly.

## Per-page notes

- **Home** (`HomePage.html`): the reference implementation. One `.card` of
  device status as `.kv` rows (Serial, Version, Network, IP address, Free
  memory); a second card of "What you can do here" with three links (Upload a
  book → /files, Change settings → /settings, Add a font → /fonts) as `.list`
  rows. Footer line: "e-Minimal · open source" in `--ink-3`, small — the only
  place a middle dot is fine.
- **Files** (`FilesPage.html`): a sticky `.toolbar` (Upload, New folder). The
  folder card heads with breadcrumbs and a summary, then "Find by name", a Sort
  `select` (Name A–Z, Largest first, Type; folders always first) and type chips
  (All, Folders, Books — .epub .txt .md .xtc .xtch —, Pictures, Fonts, Other)
  with counts, empty ones left out. `?type=<chip>` picks the chip and follows
  changes, so `/files?path=/carousel&type=pictures` opens on the pictures. Rows
  are one grid list: checkbox, type glyph, name (folders open, pictures preview,
  files download; "EPUB · 1.2 MB" under it on phones, Type and Size columns from
  700 px) and a More button whose shared menu has Download, Rename, Move, Delete
  (Open, Delete for folders: the reader renames and moves files only). Checkbox,
  a tap on a row's empty space, Shift+click or select-all (rows shown) raise a
  `.savebar` selection bar ("3 selected", Clear, "Move 2 files", "Delete 3
  items"); hidden rows leave the selection, Esc clears it. Changes re-read the
  folder in place and toast; bad input shows under its field. The failed-uploads
  banner is a `.card` with `--danger` rule. The upload modal and optimiser keep
  their ids and behaviour.
- **Settings** (`SettingsPage.html`): categories are rendered by JS from
  `/api/settings` (`s.category`); render each category as a `.card` with an id
  `section-<slug>` and build `.section-nav` from the category list (sticky list
  on laptops, `select` on phones). Rows become `.list > .row` with the control
  right-aligned; `toggle` type uses `.toggle`, `enum` a `select`, `value` a
  number input, `string` a text/password input. Wi-Fi networks and OPDS servers
  are two more sections in the same nav. The save button becomes the `.savebar`
  ("3 changes" left, "Save changes" right), shown only when something changed.
- **Dashboards** (`DashboardsPage.html`): every Dashboard setting in one page;
  see Dashboards below.
- **Convert** (`ConvertPage.html` + `js/bindery-core.js`, `js/bindery-app.js`):
  manga, comics and books made ready on the phone. Two modes (`.btn-group`):
  *Manga and comics* takes CBZ/ZIP, PDF, image EPUBs and loose pictures and
  writes XTCH (default), XTC, fixed-layout EPUB or CBZ at the reader's size;
  *Books* turns TXT/Markdown/HTML into a reflowable EPUB with detected
  chapters and a made cover, and tidies EPUB pictures (shrink, grey,
  baseline — progressive JPEGs decode at 1/8 on the reader). "Send to
  reader" streams XTC pages over the Files page's WebSocket upload as they
  are drawn (header and page table first: every page is the same size), so
  memory on the phone stays flat; "Save to this phone" keeps them instead.
  The same file builds a standalone single-page copy
  (`scripts/bindery/build_standalone.py`, `data-bindery-host="standalone"`,
  no shell). Tests and the mock reader live in `scripts/bindery/`.
- **Fonts** (`FontsPage.html`): installed fonts as a `.list` (name, sizes,
  Delete as `.btn-danger.btn-sm`); upload as a `.card` with a `.field` file
  input and the progress bar as `.progress`. Empty state: "No fonts on the card
  yet. Upload a .cpfont family to use it in books."
- **Wi-Fi setup** (`WifiSetupPage.html`, served by a different server that
  also serves `/css/app.css` but not `/js/app.js`): no shell. A single centred
  `.card` under a serif `h1` "Connect your reader to Wi-Fi"; the form and the
  status logic stay as they are; restyle with `.field`, `.btn-primary`,
  `.chip`. Keep its inline CSS to the few rules the card needs beyond app.css.
- **Phone text entry** (`TextEntryPage.html`, served by its own
  `TextEntryServer` alongside `/css/app.css`, no `/js/app.js`): same
  no-shell shape as Wi-Fi setup. A single centred `.card` under a serif `h1`
  with one `<input>` (type driven by the field's `kind`: text, password gets
  a Show toggle, or url), a "Send to reader" `.btn-primary`, and a status
  line; locks the form after a successful send ("Sent. You can close this
  page."). Fills the title and any prefilled value via `textContent`/`.value`
  only, never innerHTML — titles and filenames can contain `<`.
- **Game Night phone page** (`PlayPage.html`, served by the separate
  `GameServer`, which serves `/css/app.css` but not `/js/app.js`): no shell and
  no drawer, since the game server serves only the play page. See
  `docs/party-games.md`.

## Dashboards

`DashboardsPage.html` at `/dashboards` holds every Dashboard setting; the
reader only launches the dashboard and shows QR codes that open this page (see
Deep links). It is a normal shell page (`app.css`, `app.js`, `ui.toast`,
`ui.confirm`) and reads and writes `/api/dashboards`, `/api/todo` and, for the
orientation, `/api/settings`.

Three tabs under the topbar, a `.btn-group` (`role="tablist"`, the chosen tab
`.active`, arrow keys move between them) in a `.toolbar-sticky`: **Widgets**
(Weather, Image, To-do, Date), **Layouts** (the strip and the editor) and
**Settings** (Orientation, Starting layouts). With no fragment the page
opens on the tab used last (kept in `localStorage`, read in try/catch);
picking a tab by hand drops the fragment so a reload stays there. Each card's
id is its deep link.

Widgets shows one widget's card at a time: a "Widget" `select` at the top of
the tab (Weather, Image, To-do, Date; each card carries `data-widget`) picks
it. The choice is kept in `localStorage` (`eminimal.dashboards.widget`,
Weather when there is none); a widget's fragment picks that widget, and
picking one by hand drops the fragment. A field the reader rejects brings its
widget up first.

- **Layouts** (`#layouts`): every layout is alike: there are no presets, and
  each can be edited, renamed and deleted. The `.card-head` carries New, Edit
  and a `.btn-danger` Delete (all `.btn-sm`), which act on the layout in use.
  Below, a strip that scrolls sideways of choice cards (visually hidden radios
  in labels, 140 px each), one per layout in `layouts` order, the one in use
  inverted (ink fill, paper text) like the device's selected row, its meta
  line "In use" (the others "N tiles"), and kept in view. Each card shows an
  SVG thumbnail of the layout's own tiles with the widget name inside each,
  drawn in the shape of the current Dashboard Orientation; a layout drawn for
  the other shape is turned on its side (x/y and w/h swapped), as the reader
  does. Tapping a card puts it in use at once (`layout.active`), then a toast.
  New opens the editor on a new layout (disabled, with a note under the strip,
  once `grid.maxLayouts` exist; until then the note counts them, "4 of 12
  layouts"). Edit opens the layout in use in the editor (`#layout-<id>`).
  Delete asks first (`ui.confirm`; for the last layout it adds that the dashboard has no
  layout until one is made or the starting ones are restored), then posts
  `deleteLayout`; the toast names the layout that took over, or says none is
  left. With no layouts, the strip holds an `.empty` note ("No layouts. Create
  one, or restore the starting layouts in Settings", the second part a link to
  `#starting-layouts`) and Edit and Delete are disabled. Layout changes only
  re-read the layouts, so unsaved Weather and Image edits stay.
- **Layout editor** (`#editor`, shown in place of the Layouts card): Name
  (required, up to `grid.maxName` UTF-8 bytes, editable for every layout);
  Shape, a Landscape / Portrait `.btn-group` (switching transposes the tiles).
  A new layout starts empty in the current orientation's shape; an existing
  one opens on its own tiles and shape. Then "Add a widget", a `.toolbar` of
  `.btn-sm` buttons placing the widget at the first free spot at 4 × 3,
  shrinking to fit down to `grid.minCells`, with a toast when there is no room
  or `grid.maxTiles` is reached; the board; a "Selected tile" `.field-row`
  (widget `select`, `.btn-danger` Delete); Cancel (asks before discarding
  changes) and "Save layout". The board is the only new styling: a
  `--surface` canvas with a 1 px `--ink` border and `--rule` cell lines, its
  aspect ratio columns / rows of the chosen shape and its width capped so a
  portrait grid fits the window; tiles are `--rule-2` boxes with an `--ink`
  border, the widget name and "w × h", the picked one inverted with a 32 px
  resize grip. Pointer events drag a tile or its grip in whole cells, clamped
  to the grid; a drop over another tile shows dashed `--danger` and springs
  back. Arrow keys move the focused tile, Shift and arrows resize, Delete
  removes. Save posts `saveLayout` (the reader saves the layout and puts it in
  use), then returns to `#layouts`; a rejected `saveLayout.name` marks the
  name, `saveLayout.tiles[i]` flags and picks that tile, and a full list
  (`saveLayout`) says to delete one first.
- **Orientation** (`#orientation`): three choice cards, each a small device
  drawing with the keys where they end up (left, right, bottom), for the
  device setting `dashboardOrientation`; picking one saves it through
  `POST /api/settings` and redraws the thumbnails.
- **Starting layouts** (`#starting-layouts`): "Restore starting layouts"
  posts `restoreDefaults`, which adds back Wide top, Grid and Picture top when
  their names are gone, drawn the way the reader is turned. The toast says
  "Added N starting layouts" (the reader's `restored`) or "They're all there
  already"; with a full list it says there is no room (the page tells the
  cases apart by those three names).
- **Weather** (`#weather`): "Find a place" searches Open-Meteo's geocoding
  API from the browser
  (`geocoding-api.open-meteo.com/v1/search?name=…&count=5&language=en&format=json`)
  and lists up to five matches as `.list` rows built with `textContent`;
  picking one fills Place name, Latitude and Longitude. When the request fails
  (a hotspot with no uplink, 8 s timeout) the status line says search needs the
  internet and to type the coordinates instead. "Clear location"; Temperature
  as a °C/°F `.btn-group`.
- **Image** (`#image`): the SD folder of BMP, PNG or JPEG images (default
  `/carousel`) and a `select` of 5 min to 24 h, then two `.list` link rows:
  "Add pictures" to the Pictures page (`/pictures`, see Pictures below) and
  "Pictures in `<folder>`" to Files at
  `/files?path=<folder>&type=pictures`, its meta the count of JPEG, PNG and
  BMP files there (`GET /api/files`). Both use the saved folder; an edited,
  unsaved Folder field adds "Save changes first to use the new one", and
  saving a new folder counts that one.
- **To-do** (`#todo`): the list as `.list` rows (checkbox, text, Edit,
  Delete; a ticked task is struck through), a New task `.field-row` with a
  byte count against `maxText`, and "Clear done" (`ui.confirm`). Every change
  posts `/api/todo` at once and redraws from its answer; text is put on one
  line and cut to `maxText` bytes on a character boundary; at `max` tasks the
  input is disabled with a note.
- **Date** (`#date`): information only: the tile shows once a weather update
  has set the reader's clock.

Weather and Image share the `.savebar` ("Unsaved changes to Weather and
Image", naming whichever differ, and "Save changes"); everything else saves
as it changes. A field the reader rejects gets `aria-invalid`, focus and its
message in a toast.

### Pictures

`PicturesPage.html` at `/pictures` readies photos for the Image tile on the
phone, the way Convert readies comics, so the reader never decodes a 12 to
50 MP camera file. It is a shell page (`data-nav="dashboards"`) with a
`.crumbs` line back to Dashboards (`/dashboards#image`), and loads
`/js/bindery-core.js` (13 KB gzipped, already served for Convert) for its
tone code (`Bindery.applyTone`, `Bindery.quantize`) and its ZIP writer
(`Bindery.writeZip`). The folder is the one the reader saved, read from
`GET /api/dashboards` (`carousel.folder`); when that fails the page says so
and Upload stays off. Links from before the page existed
(`/dashboards#pictures`) land here.

- **Pick** (the "Add pictures" card): a `.dropzone` (`accept="image/*"`,
  several at once, drag and drop too); a file that is not a picture is left
  out with a toast. Each photo is a `.list` row in name order with Preview and
  Remove, under a "N pictures to add" head with Remove all.
- **Tone**: a preview of one picture (the first, or the row's Preview) beside
  Convert's own controls: Auto contrast (on), Contrast (-40 to 40), Midtones
  (0.50 to 2.00). The preview is drawn at most 960 px long, quantised to the
  reader's 16 greys, and redrawn as a slider moves. The settings are kept in
  `localStorage` (`eminimal.dashboards.pictureTone`).
- **Convert** ("Convert N pictures"): each photo is decoded with its EXIF
  turn (`createImageBitmap(file, { imageOrientation: 'from-image' })`, an
  `<img>` where that fails), shrunk in halving steps with
  `imageSmoothingQuality = 'high'` to fit 1872 × 1404 either way round (never
  enlarged, never cropped: the reader crops to each tile; transparency becomes
  white), turned grey and toned, not dithered, and encoded with
  `canvas.toBlob('image/jpeg', 0.9)`, a baseline JPEG. It is named after the
  photo with `.jpg`; a name the folder or the batch already has gets " (2)",
  " (3)"… Changing the tone or adding photos brings Convert back.
- **Then either**: **Upload to reader** sends them one at a time over the
  Files page's WebSocket upload (port + 1, `START:<name>:<size>:<folder>`),
  or its `POST /upload?path=` when that is not there. It makes the folder with
  `/mkdir` when the listing comes back empty and moves on to the next free
  name if the reader answers "File already exists". Each row shows its
  percentage and then "On the reader as `<name>`". **Save to this device**
  downloads one picture as itself or several as `eminimal-pictures.zip`,
  stored rather than deflated. A hint says to copy them into the folder with a
  card reader. Progress and a Cancel button sit under the buttons, and the
  screen is kept awake while it runs.
- **Done**: when a run ends (finished or cancelled), the pictures that went
  onto the card, or into the download, leave the batch and their JPEGs are
  dropped; the ones that failed stay, marked, to try again or remove. The
  progress line stays under the dropzone with the result ("6 pictures added
  to /carousel.") until more photos are added, and a toast sums it up.
- **On the card**: no list of files. One `.list` link row, "Pictures in
  `<folder>`", to Files at `/files?path=<folder>&type=pictures`, its meta the
  count of JPEG, PNG and BMP files there (`GET /api/files`, recounted after
  an upload; the same listing avoids names already taken), and a "Change the
  folder" row to `/dashboards#image`. Over 500, a note says the reader shows
  500.

### Deep links

The reader's QR codes open `/dashboards` with a fragment; after loading, the
page picks that tab and opens or scrolls to that part, and follows later
changes of the fragment (the Edit and New buttons and the editor's Save and
Cancel go through it too):

| Fragment | Opens |
|----------|-------|
| none | The tab used last, at the top |
| `#layouts` | Layouts |
| `#layout-new` | Layouts, the editor on a new layout (a note instead when the list is full) |
| `#layout-<id>` | Layouts, the editor on that layout (a note and `#layouts` when it is gone) |
| `#preset-a`, `#preset-b`, `#preset-c` | Links from before every layout was editable: Layouts, the fragment replaced with `#layouts` |
| `#weather`, `#image`, `#todo`, `#date` | Widgets, that widget picked |
| `#pictures` | The Pictures page (`/pictures`), which used to be part of the Image card |
| `#orientation` | Settings |
| `#starting-layouts` | Settings, scrolled to Starting layouts |
