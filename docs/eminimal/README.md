# e-Minimal firmware: developer documentation

| | |
|---|---|
| **Status** | Draft, fact-checked against the source on 2026-11-22 |
| **Applies to** | branch `master`, written against commit `36f4315e` |
| **Audience** | Firmware developers and contributors |

e-Minimal is a fork of [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader) for a
7.8" e-ink reader built around an ESP32-S3 and an IT8951 display controller. This folder documents how the
fork is built: the architecture, the link from the ESP32 to the display, the application framework, and the
features the fork adds (dashboard, party games, firmware install over Wi-Fi).

The documents in the parent `docs/` folder come from the upstream project and describe user-level behaviour
and file formats. Where the fork's code differs from them, the documents below say so.

## Documents

| # | Document | Answers |
|---|---|---|
| 1 | [System overview](01-system-overview.md) | What the system is, its layers, boot sequence, tasks and memory, how the fork differs from upstream |
| 2 | [Display pipeline](02-display-pipeline.md) | How the ESP32 talks to the display: pins, SPI, IT8951 protocol, framebuffer, refresh modes, power |
| 3 | [Application framework](03-application-framework.md) | Activities, navigation, input, settings and state, themes, fonts, sleep and wake |
| 4 | [Dashboard and games](04-dashboard-and-games.md) | How the dashboard and Game Night are built and integrated; how to add a widget or a game |
| 5 | [Network and storage](05-network-and-storage.md) | Wi-Fi, web servers and endpoints, firmware update, security facts, SD card layout, persistent stores |
| 6 | [Build, flash and release](06-build-flash-release.md) | Build environments, partitions, build scripts, tests, release path |

## Reading order

- New to the code base: 1, then 3.
- Working on the panel or refresh quality: 2.
- Adding a screen, widget or game: 3, then 4.
- Working on file transfer, phone pages or updates: 5.

## Conventions

- Every non-trivial statement names the source file and symbol it was taken from, as a repo-relative path.
- Each document ends with an "Open questions" section listing what could not be established from the code.
  Treat those items as unverified.
- Diagrams are written in Mermaid and render on GitHub.
- A document is updated in the same change as the code it describes. Update the "Applies to" commit when
  you do.

## Licence

The firmware is open source under the MIT licence (see `LICENSE` and `NOTICE.md` at the repository root).
The hardware design files for the device are not part of this repository.
