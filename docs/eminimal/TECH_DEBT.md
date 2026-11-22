# Technical debt and known gaps

| | |
|---|---|
| **Status** | Living list, started 2026-11-22 |
| **Audience** | Maintainers and contributors looking for something to pick up |

Each item names where the problem is and what a fix involves. Items marked *from code reading* were found by
reading the source and have not been reproduced on a device. Remove an item in the pull request that fixes it.

## Features not finished

| # | Item | Where | What is needed |
|---|---|---|---|
| F1 | **Over-the-air update from releases.** "Check for Updates" is compiled out on e-Minimal because the updater only knows the upstream project's release assets. Updates are installed from the SD card or pushed over Wi-Fi. | `src/network/OtaUpdater.cpp` (`latestReleaseUrl`, asset name), `src/activities/settings/SettingsActivity.cpp` (`#if !FREEINK_DEVICE_EMINIMAL`) | (1) A release workflow that builds `-e eminimal` and attaches the image to a GitHub Release of this repository. (2) Point the updater at this repository's releases and asset name, and restore the menu entry. (3) Test on a device, including a failed download and a bad image. Step 1 is useful by itself: the release file can be installed with the existing SD-card or Wi-Fi install. |
| F2 | **Release procedure.** There is no tag convention, release workflow or changelog for e-Minimal builds. | `.github/workflows/release*.yml` (upstream's), `platformio.ini` `[eminimal] version` | Decide the tag format, add the workflow from F1, document the steps in `06-build-flash-release.md`. |
| F3 | **Wi-Fi push tool.** The firmware install screen and `docs/eminimal-controls.md` refer to a push script that is not in this repository. | `src/network/FirmwareInstallServer.*` | Publish a small client for `POST /api/firmware`, or document the request so one can be written. |

## Security

| # | Item | Where | What is needed |
|---|---|---|---|
| S1 | **HTTPS certificates are not verified.** Every outgoing HTTPS request (weather, OPDS, downloads) runs with verification off. The class comment says the opposite. | `src/network/HttpDownloader.cpp` (`runGetWolf`, `setInsecure()`), `platformio.ini` (`FREEINK_NET_WOLFSSL=1`) | Load a CA bundle into the wolfSSL path, or pin the hosts the firmware calls. Correct the comment. |
| S2 | **Credentials follow redirects.** The `Authorization` header is re-sent on every redirect hop, whatever the host. *From code reading.* | `src/network/HttpDownloader.cpp` (`runGetWolf`) | Drop the header when the redirect leaves the original host. |
| S3 | **Firmware install over Wi-Fi has no access code and images are not signed.** Any host on the joined network can stage an image. Flashing still needs a 3-second key hold on the device. | `src/network/FirmwareInstallServer.cpp`, `src/network/FirmwareFlasher.cpp` | Require the session code used by file transfer; consider image signing. |
| S4 | **WebDAV copy and move can overwrite at Upload access level.** *From code reading.* | `src/network/WebDAVHandler.cpp` (`handleCopy`, `handleMove`) | Require Full access when the destination exists. |
| S5 | **A stored access level above the valid range is treated as Full.** | `src/util/WebSession.cpp` | Treat an out-of-range value as the most restrictive level. |

## Defects

| # | Item | Where | What is needed |
|---|---|---|---|
| D1 | **Dashboard focus frame is drawn on unattended timer wakes.** The constructor hides it, `onEnter` turns it back on. *From code reading.* | `src/activities/dashboard/TiledDashboardActivity.cpp` (`onEnter`) | Keep `showFocus` false when the wake was a timer wake. |
| D2 | **Two clocks that are not kept in step.** The date tile uses the system clock set from the weather reply; the to-do screen uses the hardware clock set by NTP. The time zones come from different places. | `src/activities/dashboard/WeatherService.cpp`, `DashboardTiles.cpp`, `lib/hal/HalClock.*` | One time source and one time-zone setting. |
| D3 | **Adding a fourth default dashboard layout reads past the preset key table.** | `src/DashboardConfigStore.cpp` (`PRESET_KEYS`, `migratePresets`) | Bound the migration loop to the three legacy presets. |
| D4 | **Game Night ignores the "Announce on Network" setting** and always advertises `eminimal.local`. Whether this is intended is not recorded. | `src/activities/games/GameNightActivity.cpp`, `src/network/PhonePortal.cpp` | Decide, then honour the setting or document the exception. |

## Documentation and comments that are out of date

| # | Item | Where |
|---|---|---|
| C1 | Game Night's hotspot is described as open. It is WPA2 with a random passphrase per session. | `docs/party-games.md`, comments in `src/activities/games/GameNightActivity.{h,cpp}` |
| C2 | Web server security notes say there is no authentication and an open hotspot. | `docs/webserver.md`, the Network Modes section of `docs/webserver-endpoints.md` |
| C3 | Power menu rows and the Time to Sleep control are described as they were before the Global Settings change. | `docs/eminimal-controls.md` |
| C4 | `book.bin` is documented at version 10; the code is at 11. | `docs/file-formats.md` |
| C5 | Describes a single-core ESP32-C3 with an 8 KB render stack, and functions that no longer exist. | `docs/activity-manager.md`, `AGENTS.md` |
| C6 | Header comment says the SD update uses the Arduino Update API. | `src/activities/settings/SdFirmwareUpdateActivity.h` |

## Project housekeeping

| # | Item | What is needed |
|---|---|---|
| H1 | `GOVERNANCE.md`, `SCOPE.md`, `ROADMAP.md`, the funding file and the feature-request and pull-request templates are the upstream project's. | Replace with the fork's own, or remove. |
| H2 | No procedure is written down for bringing upstream changes into the fork. | Document it (rebase or merge, how conflicts in shared files are handled) in `06-build-flash-release.md`. |
| H3 | The party games use the names of commercial games. | Decide whether to rename them. |
| H4 | The origin of the word list is not recorded. | `lib/PartyGames/WordBank.cpp`: state the source and its licence. |
