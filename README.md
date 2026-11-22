# e-Minimal firmware

Open-source firmware for **e-Minimal**, a home-built 7.8" e-ink reader based on an ESP32-S3.

It is a fork of [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader), the community
e-reader firmware. CrossPoint supplies the reading engine, the user interface and the web tools. This fork
adds support for the e-Minimal hardware and a few features of its own.

> Not affiliated with or endorsed by the CrossPoint Reader project, Xteink or Waveshare.

## What it does

**From CrossPoint**

- Reads EPUB 2/3, XTC/XTCH, TXT and Markdown from a microSD card, with hyphenation, kerning, bookmarks,
  footnotes, dictionary lookup and custom fonts.
- A library with covers, recent books, series and genre views.
- File transfer from a phone or computer over Wi-Fi (web page, WebDAV), and OPDS catalogues.

**Added by this fork**

- **The e-Minimal board**: ESP32-S3 with a 1872 x 1404 panel driven through an IT8951 controller, including
  an IT8951 driver with partial updates and automatic waveform selection.
- **Five-key control**: four keys under the screen plus a power key, with a power menu and an app switcher.
- **Dashboard**: a screen of tiles (date, weather, to-do list, pictures) that refreshes on a timer and
  sleeps between updates.
- **Game Night**: party games where the reader is the shared board and each player's phone is a private
  hand, joined by scanning a QR code. No internet needed.
- **Phone-assisted setup**: Wi-Fi setup and text entry from a phone, since the device has no keyboard.
- **Firmware install over Wi-Fi or from the SD card**, with a confirmation on the device.
- **Web access control**: an access code and access levels for the file-transfer pages.

## Hardware

The firmware targets the parts below. The custom circuit board and the case of the finished device are not
published; the firmware runs the same on a development board wired as shown.

| Part | Used here |
|---|---|
| MCU | ESP32-S3-WROOM-1 **N16R8** (16 MB flash, 8 MB octal PSRAM), for example an ESP32-S3-DevKitC-1 |
| Display | 7.8" e-ink panel, 1872 x 1404, with an IT8951 controller board (SPI) |
| Storage | microSD on the SDMMC pins (1-bit) |
| Keys | Five push buttons to ground |
| Battery (optional) | LiPo with a MAX17048 fuel gauge on I2C |

| Signal | GPIO | | Signal | GPIO |
|---|---|---|---|---|
| Display SCLK | 14 | | Key Back | 7 |
| Display MOSI | 16 | | Key Confirm | 6 |
| Display MISO | 17 | | Key Up | 4 |
| Display CS | 18 | | Key Down | 5 |
| Display HRDY (busy) | 38 | | Key Power | 15 |
| Display reset | 40 | | SD CLK / CMD / D0 | 12 / 11 / 13 |
| Display power enable | 39 | | SD power switch | 21 |
| Fuel gauge SDA / SCL | 41 / 42 | | USB detect | 9 |

GPIO 33 to 37 are used by the PSRAM and must stay free. The full table with notes is in
[docs/eminimal/01-system-overview.md](docs/eminimal/01-system-overview.md).

## Build and flash

You need [PlatformIO](https://platformio.org/) and git.

```sh
git clone --recursive https://github.com/SaiduthE/e-Minimal.git
cd e-Minimal
pio run -e eminimal              # build
pio run -e eminimal -t upload    # build and flash over USB
pio device monitor -e eminimal   # serial console
```

Always pass `-e eminimal`. The default environment builds upstream's firmware for a different chip.
Details, partitions and troubleshooting are in
[docs/eminimal/06-build-flash-release.md](docs/eminimal/06-build-flash-release.md).

## Documentation

| For | Read |
|---|---|
| Using the device | [USER_GUIDE.md](USER_GUIDE.md), [docs/eminimal-controls.md](docs/eminimal-controls.md), [docs/party-games.md](docs/party-games.md) |
| Understanding the code | [docs/eminimal/](docs/eminimal/README.md): system overview, display pipeline, application framework, dashboard and games, network and storage, build and release |
| Contributing | [CONTRIBUTING.md](CONTRIBUTING.md) |

## Repository layout

| Path | Contents |
|---|---|
| `src/` | The application: activities (screens), stores, network servers |
| `lib/` | Libraries: EPUB engine, renderer, fonts, HAL, party-game engines |
| `freeink-sdk/` | Submodule: display, input, storage and board support (a fork of FreeInk SDK) |
| `docs/eminimal/` | Developer documentation for this fork |
| `docs/` | Upstream's user and format documentation |
| `scripts/` | Build-time generators (web pages, fonts, translations) |
| `test/` | Host unit tests |

## Relationship to upstream

The fork is based on CrossPoint's `develop` branch and keeps upstream's files in place so changes can be
brought across. `GOVERNANCE.md`, `SCOPE.md`, `ROADMAP.md` and `AGENTS.md` are upstream's documents and
describe the upstream project, not this fork. Fixes that are not specific to e-Minimal are better sent to
[CrossPoint](https://github.com/crosspoint-reader/crosspoint-reader) directly.

## Licence

MIT. See [LICENSE](LICENSE) and [NOTICE.md](NOTICE.md) for attribution and third-party components.
The hardware design files of the device are not part of this repository and are not published.
