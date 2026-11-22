# Notice

## This fork

e-Minimal firmware is a fork of **CrossPoint Reader**
(https://github.com/crosspoint-reader/crosspoint-reader), which is distributed under the MIT licence,
Copyright (c) 2025 CrossPoint Reader organization. The fork's changes are distributed under the same MIT
licence (see `LICENSE`).

The fork is not affiliated with or endorsed by the CrossPoint Reader project, Xteink or Waveshare.
`GOVERNANCE.md`, `SCOPE.md`, `ROADMAP.md`, `AGENTS.md` and most of `USER_GUIDE.md` are the
upstream project's documents and describe the upstream project.

## What is open source and what is not

| Part | Where | Status |
|---|---|---|
| Firmware, including the dashboard and the party games | this repository | Open source, MIT |
| Display and board support SDK | `freeink-sdk/` submodule (a fork of FreeInk SDK) | Open source, MIT; see `freeink-sdk/NOTICE` |
| Developer documentation | `docs/eminimal/` | Open source, MIT |
| Circuit board design, case design, review material, bench notes and companion tools | not in this repository | Not published |

## Third-party components

The licences below are those stated by each project. Check the component itself before relying on this table.

| Component | Location | Licence |
|---|---|---|
| Expat | `lib/expat` | MIT |
| miniz | `lib/miniz` | MIT |
| uzlib | `lib/uzlib` | zlib |
| pdf.js (Mozilla Foundation) | `src/network/html/js/pdf.min.js`, `pdf.worker.min.js` | Apache-2.0 (https://www.apache.org/licenses/LICENSE-2.0) |
| Lucide icons, rasterised | `src/components/icons/` | ISC (https://lucide.dev/license) |
| Noto Sans, Noto Serif, OpenDyslexic | `lib/EpdFont/builtinFonts/` | SIL Open Font License 1.1 (`OFL.txt` beside the sources) |
| Ubuntu font | `lib/EpdFont/builtinFonts/` | Ubuntu Font Licence 1.0 (`UFL.txt` beside the sources) |
| FreeInk SDK and the libraries it bundles | `freeink-sdk/` | MIT and others; see `freeink-sdk/NOTICE` |
| Libraries fetched by PlatformIO | `platformio.ini` (`lib_deps`) | each under its own licence |

## Game names

The party games implement well-known game mechanics. Names of commercial games that appear in the source
or documentation are trademarks of their owners and are used only to describe the mechanics.
