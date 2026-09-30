# Webserver Endpoints

This document describes the HTTP, WebSocket, WebDAV, and discovery endpoints
available while CrossPoint Reader is in File Transfer or Calibre Wireless mode.

- HTTP server: port 80
- WebSocket upload server: port 81
- UDP discovery listener: port 8134
- WebDAV: port 80, handled by the same HTTP server

Examples use `crosspoint.local`. If mDNS does not resolve on your network, use
the IP address shown on the device screen.

## HTTP Pages

| Method | Path | Purpose |
|--------|------|---------|
| `GET` | `/` | Home/status page |
| `GET` | `/files` | File manager page |
| `GET` | `/settings` | Web settings page |
| `GET` | `/dashboards` | Dashboards page: layouts and their editor, orientation, widget settings, to-do list |
| `GET` | `/pictures` | Pictures page: photos shrunk and greyed on the phone, then uploaded into the Image widget's folder |
| `GET` | `/fonts` | SD-card font manager page |
| `GET` | `/js/jszip.min.js` | JavaScript asset used by the file manager |

## Device Status

### `GET /api/status`

```bash
curl http://crosspoint.local/api/status
```

Response:

```json
{
  "version": "1.0.0",
  "ip": "192.168.1.100",
  "mode": "STA",
  "rssi": -45,
  "freeHeap": 123456,
  "uptime": 3600,
  "device": "X4"
}
```

| Field | Type | Description |
|-------|------|-------------|
| `version` | string | Firmware version |
| `ip` | string | Device IP address |
| `mode` | string | `"STA"` for joined Wi-Fi or `"AP"` for hotspot mode |
| `rssi` | number | Wi-Fi RSSI in dBm; `0` in AP mode |
| `freeHeap` | number | Free heap in bytes |
| `uptime` | number | Seconds since boot |
| `device` | string | `"X3"` or `"X4"` hardware detection |

## File Management

### `GET /api/files`

Lists files and folders under a directory.

```bash
curl "http://crosspoint.local/api/files?path=/Books"
```

Query parameters:

| Parameter | Required | Default | Description |
|-----------|----------|---------|-------------|
| `path` | No | `/` | Directory to list |

Response:

```json
[
  {"name":"MyBook.epub","size":1234567,"isDirectory":false,"isEpub":true},
  {"name":"Notes","size":0,"isDirectory":true,"isEpub":false}
]
```

Hidden dotfiles are omitted unless the device setting `showHiddenFiles` is
enabled. `System Volume Information` and `XTCache` are always hidden/protected.

### `GET /download`

Downloads a file from the SD card.

```bash
curl -OJ "http://crosspoint.local/download?path=/Books/MyBook.epub"
```

Query parameters:

| Parameter | Required | Description |
|-----------|----------|-------------|
| `path` | Yes | File path to download |

Protected dotfiles, `System Volume Information`, and `XTCache` cannot be
downloaded. EPUB files are served as `application/epub+zip`; other files use
`application/octet-stream`.

### `POST /upload`

Uploads a file with HTTP multipart form data.

```bash
curl -X POST -F "file=@mybook.epub" "http://crosspoint.local/upload?path=/Books"
```

Query parameters:

| Parameter | Required | Default | Description |
|-----------|----------|---------|-------------|
| `path` | No | `/` | Destination directory |

Successful response:

```text
File uploaded successfully: mybook.epub
```

Notes:

- Existing files with the same name are overwritten.
- EPUB cache data for the uploaded path is cleared after a successful upload.
- HTTP upload uses a 4 KB write buffer before flushing to the SD card.

### `POST /mkdir`

Creates a folder.

```bash
curl -X POST -d "name=NewFolder&path=/" http://crosspoint.local/mkdir
```

Form parameters:

| Parameter | Required | Default | Description |
|-----------|----------|---------|-------------|
| `name` | Yes | - | New folder name |
| `path` | No | `/` | Parent folder |

### `POST /rename`

Renames a file.

```bash
curl -X POST -d "path=/Books/old.epub&name=new.epub" http://crosspoint.local/rename
```

Form parameters:

| Parameter | Required | Description |
|-----------|----------|-------------|
| `path` | Yes | Existing file path |
| `name` | Yes | New file name, not a path |

Only files can be renamed through this endpoint. The old EPUB cache path is
cleared before the rename.

### `POST /move`

Moves a file into an existing folder.

```bash
curl -X POST -d "path=/Books/mybook.epub&dest=/Read" http://crosspoint.local/move
```

Form parameters:

| Parameter | Required | Description |
|-----------|----------|-------------|
| `path` | Yes | Existing file path |
| `dest` | Yes | Existing destination folder |

Only files can be moved through this endpoint. The old EPUB cache path is
cleared before the move.

### `POST /delete`

Deletes one or more files or empty folders.

```bash
curl -X POST -d "path=/Books/mybook.epub" http://crosspoint.local/delete
curl -X POST -d 'paths=["/Books/old.epub","/OldFolder"]' http://crosspoint.local/delete
```

Form parameters:

| Parameter | Required | Description |
|-----------|----------|-------------|
| `path` | Yes, unless `paths` is provided | Single path to delete |
| `paths` | Yes, unless `path` is provided | JSON array of paths to delete |

Protected items cannot be deleted. Non-empty folders are rejected. EPUB cache
data for deleted files is cleared.

## Settings API

### `GET /api/settings`

Returns a streamed JSON array of editable settings. Each item contains common
fields plus type-specific fields.

```bash
curl http://crosspoint.local/api/settings
```

Example item:

```json
{
  "key": "fontSize",
  "name": "Reader Font Size",
  "category": "Reader",
  "type": "enum",
  "value": 1,
  "options": ["12 pt", "14 pt", "16 pt", "18 pt"]
}
```

`value` is always an index into `options`, never the option's text. The
`fontSize` options depend on the selected family. A `.cpfont` family installed
at 10/12/14 pt offers those three sizes. TTF/OTF/TTC families offer the
standard 12/14/16/18 pt sizes. The `fontFamily` and `dictionaryName` options
also depend on the SD card contents.

Types:

| Type | Extra fields |
|------|--------------|
| `toggle` | `value` (`0` or `1`) |
| `enum` | `value`, `options` |
| `value` | `value`, `min`, `max`, `step` |
| `string` | `value` |

The font-family setting includes SD-card font families when they are installed.

### `POST /api/settings`

Applies a partial settings update from a JSON object.

```bash
curl -X POST \
  -H "Content-Type: application/json" \
  -d '{"fontSize":2,"showHiddenFiles":1}' \
  http://crosspoint.local/api/settings
```

Successful response:

```text
Applied 2 setting(s)
```

## Dashboards API

Every Dashboard setting lives here, behind the web UI's Dashboards page; the
reader only launches the dashboard and shows QR codes that open that page.
The settings are stored on the SD card in `/.crosspoint/dashboards.json`; a
missing file means the defaults below. Which way the dashboard is turned is
the device setting `dashboardOrientation` (`POST /api/settings`): `0`
landscape with the keys on the left, `1` landscape with the keys on the right,
`2` portrait with the keys at the bottom.

The dashboard shows one layout. Layouts are drawn in the page's layout editor
on a grid of square cells: `grid.long` by `grid.short` in landscape (today 14
by 10), the other way round in portrait. Each has a `shape`; on a dashboard
turned the other way it is shown turned on its side (x and y swapped). Widget
keys: `weather`, `todo`, `image`, `date`.

Every layout can be edited, renamed and deleted, the last one too. Without a
settings file there are three, "Wide top", "Grid" and "Picture top", ordinary
layouts like any other; `restoreDefaults` adds back the ones that are gone. A
file saved by older firmware, with presets A/B/C beside custom layouts, is
read with its presets turned into ordinary layouts and its custom layouts kept.

### `GET /api/dashboards`

```bash
curl http://crosspoint.local/api/dashboards
```

Response (arrays shortened):

```json
{
  "layout": {"active": 4},
  "orientation": 0,
  "grid": {"long": 14, "short": 10, "minCells": 2, "maxTiles": 8, "maxName": 32, "maxLayouts": 12},
  "layouts": [
    {
      "id": 1,
      "name": "Wide top",
      "shape": "landscape",
      "tiles": [
        {"widget": "weather", "x": 0, "y": 0, "w": 14, "h": 4},
        {"widget": "todo", "x": 0, "y": 4, "w": 7, "h": 6},
        {"widget": "image", "x": 7, "y": 4, "w": 7, "h": 6}
      ]
    },
    {
      "id": 4,
      "name": "Desk",
      "shape": "landscape",
      "tiles": [
        {"widget": "todo", "x": 0, "y": 0, "w": 7, "h": 10},
        {"widget": "weather", "x": 7, "y": 0, "w": 7, "h": 4}
      ]
    }
  ],
  "weather": {"hasLocation": true, "place": "Berlin", "lat": 52.5244, "lon": 13.4105, "units": "c"},
  "carousel": {"folder": "/carousel", "intervalMinutes": 30, "minMinutes": 5, "maxMinutes": 1440}
}
```

| Field | Type | Description |
|-------|------|-------------|
| `layout.active` | number | Id of the layout on the dashboard; `0` when none is (every layout was deleted) |
| `orientation` | number | The device setting `dashboardOrientation` (read only here) |
| `grid` | object | `long`, `short` (grid sides in cells), `minCells` (smallest tile side), `maxTiles` (per layout), `maxName` (name bytes), `maxLayouts` (layouts kept) |
| `layouts` | array | Every layout, in the order they were made: `id`, `name`, `shape` (`landscape` / `portrait`), `tiles` (`widget`, `x`, `y`, `w`, `h` in cells from the top left) |
| `weather.hasLocation` | boolean | Whether a place is set; without one the Weather tile asks for it |
| `weather.place` | string | Name shown on the dashboard; `""` with no location |
| `weather.lat`, `weather.lon` | number | Coordinates in decimal degrees, rounded to 4 places; `0` with no location |
| `weather.units` | string | `"c"` or `"f"` |
| `carousel.folder` | string | SD folder of BMP, PNG or JPEG images for the Image tile; default `/carousel` |
| `carousel.intervalMinutes` | number | Minutes each image stays up; default `30` |
| `carousel.minMinutes`, `carousel.maxMinutes` | number | Allowed interval range |

### `POST /api/dashboards`

Applies a partial update; every key is optional. The keys are applied in the
order of this table, and a rejected one saves nothing.

```bash
curl -X POST \
  -H "Content-Type: application/json" \
  -d '{"layout":{"active":4}}' \
  http://crosspoint.local/api/dashboards
```

| Field | Rule |
|-------|------|
| `layout.active` | Puts that layout on the dashboard. An unknown id is rejected as `layout.active`; `0` (a GET's "none") passes only while no layout is in use |
| `saveLayout` | `{id, name, shape, tiles}`: `id` `0` (or none) adds a layout, another id replaces that one, name included. Saved, then put in use; the answer carries its id. The name must be 1 to 32 bytes of UTF-8 without control characters; 1 to 8 tiles, each a widget other than `none`, at least `minCells` on a side, inside the grid of its `shape` and clear of the others. Problems are rejected as `saveLayout.name`, `saveLayout.shape`, `saveLayout.tiles`, `saveLayout.tiles[i]` or `saveLayout.id` (not on the reader); a full list (`grid.maxLayouts`) as `saveLayout`, with the error "No room for another layout" |
| `deleteLayout` | Deletes that layout, whichever it is. When it was in use, the first remaining layout takes over; after the last one, none is in use (`layout.active` `0`). An unknown id is rejected as `deleteLayout` |
| `restoreDefaults` | `true` adds back each starting layout (Wide top, Grid, Picture top) whose name is missing, while there is room, drawn for the current `dashboardOrientation`; if no layout was in use, the first one is then. The answer carries `restored`, how many were added (`0` when all are there or the list is full). A value that is not a boolean is rejected as `restoreDefaults` |
| `weather.lat`, `weather.lon` | Set the location only when present, and then both are required: latitude -90 to 90, longitude -180 to 180. `place` goes with them (missing means `""`; the reader keeps 64 bytes). Ignored when `weather.hasLocation` is `false`, so an echoed GET with no location does not set 0,0 |
| `weather.clearLocation` | `true` clears the location; it wins over `lat`/`lon` in the same body |
| `weather.units` | `"c"` or `"f"` |
| `carousel.folder` | Absolute SD path without `..`, at most 128 bytes; a trailing `/` is dropped |
| `carousel.intervalMinutes` | Whole minutes, 5 to 1440 |

Successful response, with `id` only after a `saveLayout` and `restored` only
after `restoreDefaults`:

```json
{"ok":true,"id":3}
```

```json
{"ok":true,"restored":2}
```

A rejected value answers `400` and saves nothing; `field` names the first bad
key:

```json
{"error":"Invalid value","field":"saveLayout.tiles[1]"}
```

A missing or malformed body also answers `400` with an `error`; a failed SD
write answers `500`.

## To-do API

The To-do tile's list, on the SD card in `/.crosspoint/todo.json`: up to 50
tasks of up to 80 bytes of UTF-8 each. Each request reads the file, acts and
saves it.

### `GET /api/todo`

```json
{"items":[{"text":"Buy milk","done":false},{"text":"Call Anna","done":true}],"max":50,"maxText":80}
```

### `POST /api/todo`

```bash
curl -X POST \
  -H "Content-Type: application/json" \
  -d '{"op":"add","text":"Water the plants"}' \
  http://crosspoint.local/api/todo
```

| `op` | Also needs | Does |
|------|------------|------|
| `add` | `text` | Adds a task at the end |
| `edit` | `index`, `text` | Replaces a task's text |
| `toggle` | `index` | Ticks or unticks a task |
| `delete` | `index` | Removes a task |
| `clearDone` | | Removes every ticked task (none is not an error) |

`index` counts from 0 in the list's order. `text` must be 1 to 80 bytes of
well-formed UTF-8 without control characters. Success answers the updated list
in `GET`'s shape. A problem answers `400` with `field`: `op`, `text`, `index`,
or `items` when the list is full (`{"error":"The list is full","field":"items"}`).

## Font Management API

### `GET /api/fonts`

Lists installed SD-card font families. On devices that load TTF/OTF/TTC files,
these families appear with `sizes: [0]`. The `0` means that the font file has
no fixed point size; the reader offers 12, 14, 16, and 18 pt.

```bash
curl http://crosspoint.local/api/fonts
```

Response:

```json
{
  "maxFamilies": 128,
  "families": [
    {
      "name": "Literata",
      "sizes": [12, 14, 16, 18],
      "files": [
        {"name": "Literata_12.cpfont", "size": 123456}
      ]
    }
  ]
}
```

### `POST /api/fonts/upload`

Uploads one `.cpfont` file into a family folder.

```bash
curl -X POST \
  -F "family=Literata" \
  -F "file=@Literata_12.cpfont" \
  http://crosspoint.local/api/fonts/upload
```

The handler validates the family name, `.cpfont` filename, and `CPFONT` magic
bytes before accepting the file.

Successful response:

```json
{"ok":true}
```

### `POST /api/fonts/delete`

Deletes an installed font family.

This endpoint removes family subfolders. To remove a loose TTF/OTF/TTC file
from a font root, delete the file from the SD card instead.

```bash
curl -X POST \
  -H "Content-Type: application/json" \
  -d '{"family":"Literata"}' \
  http://crosspoint.local/api/fonts/delete
```

Successful response:

```json
{"ok":true}
```

## OPDS Server API

### `GET /api/opds`

Lists saved OPDS servers. Passwords are never returned.

```bash
curl http://crosspoint.local/api/opds
```

Response:

```json
[
  {
    "index": 0,
    "name": "My Catalog",
    "url": "http://calibre.local:8080/opds",
    "username": "reader",
    "hasPassword": true
  }
]
```

### `POST /api/opds`

Adds or updates an OPDS server. Include `index` to update an existing entry.
If `password` is omitted during an update, the existing password is preserved.

```bash
curl -X POST \
  -H "Content-Type: application/json" \
  -d '{"name":"My Catalog","url":"http://calibre.local:8080/opds","username":"reader","password":"secret"}' \
  http://crosspoint.local/api/opds
```

### `POST /api/opds/delete`

Deletes an OPDS server by index.

```bash
curl -X POST \
  -H "Content-Type: application/json" \
  -d '{"index":0}' \
  http://crosspoint.local/api/opds/delete
```

## Wi-Fi Credential API

### `GET /api/wifi`

Lists saved Wi-Fi networks. Passwords are never returned.

```bash
curl http://crosspoint.local/api/wifi
```

Response:

```json
[
  {
    "index": 0,
    "ssid": "HomeWiFi",
    "hasPassword": true,
    "isLastConnected": true
  }
]
```

### `POST /api/wifi`

Adds or updates a saved Wi-Fi network. Include `index` to update an existing
entry. If `password` is omitted during an update, the existing password is
preserved.

```bash
curl -X POST \
  -H "Content-Type: application/json" \
  -d '{"ssid":"HomeWiFi","password":"secret"}' \
  http://crosspoint.local/api/wifi
```

### `POST /api/wifi/delete`

Deletes a saved Wi-Fi network by index.

```bash
curl -X POST \
  -H "Content-Type: application/json" \
  -d '{"index":0}' \
  http://crosspoint.local/api/wifi/delete
```

## WebSocket Upload

### Port 81

The WebSocket path is used for fast binary uploads from the file manager and
Calibre plugin workflows.

Connection:

```text
ws://crosspoint.local:81/
```

Protocol:

1. Client sends text: `START:<filename>:<size>:<path>`
2. Server replies `READY`
3. Client sends binary chunks
4. Server sends `PROGRESS:<received>:<total>` every 64 KB or at completion
5. Server sends `DONE` when complete or `ERROR:<message>` on failure

Example session:

```text
Client -> START:mybook.epub:1234567:/Books
Server -> READY
Client -> [binary chunk]
Server -> PROGRESS:65536:1234567
...
Server -> DONE
```

Error messages include:

| Message | Cause |
|---------|-------|
| `ERROR:Upload already in progress` | A second upload was started before the first completed |
| `ERROR:Invalid START format` | Malformed START message or invalid size token |
| `ERROR:Failed to create file` | Destination file could not be opened |
| `ERROR:No upload in progress` | Binary data arrived without a matching START |
| `ERROR:Upload overflow` | Client sent more bytes than declared |
| `ERROR:Write failed - disk full?` | SD write failed |

Incomplete WebSocket uploads are deleted on disconnect or error.

## WebDAV

The same HTTP server registers a WebDAV-compatible handler for file manager clients.

Supported methods:

```text
OPTIONS, GET, HEAD, PUT, DELETE, PROPFIND, MKCOL, MOVE, COPY, LOCK, UNLOCK
```

Notes:

- `PUT` writes to a temporary `.davtmp` file first, then renames it into place.
- Protected paths are rejected.
- `LOCK` and `UNLOCK` are accepted for client compatibility only. The server
  does not implement full WebDAV Class 2 locking semantics such as persistent
  locks or lock discovery.

## UDP Discovery

The server listens on UDP port `8134`. When it receives the text payload
`hello`, it replies to the sender with:

```text
crosspoint (on <hostname>);81
```

The final field is the WebSocket upload port.

## Network Modes

### Station Mode (STA)

- Device joins an existing 2.4 GHz Wi-Fi network.
- `crosspoint.local` is advertised with mDNS when available.
- `/api/status` returns `"mode": "STA"` and RSSI in dBm.

### Access Point Mode (AP)

- Device creates an open hotspot named `CrossPoint-Reader`.
- The device shows a Wi-Fi QR code and URL QR code.
- The fallback IP is typically `192.168.4.1`.
- `/api/status` returns `"mode": "AP"` and `"rssi": 0`.

### Calibre Wireless

Calibre Wireless starts the same web server in STA mode and displays setup
instructions plus WebSocket upload progress on the device screen.
