# Contributing to e-Minimal firmware

Contributions are welcome: bug reports, fixes, new dashboard tiles, new games, documentation.

## Where a change belongs

- **Specific to e-Minimal** (the IT8951 display, the five-key controls, dashboard, Game Night, phone setup,
  firmware install): here.
- **General reader behaviour** (EPUB rendering, fonts, library, file transfer): consider sending it to
  [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader) first. The fix then reaches
  more devices, and this fork picks it up from upstream.
- **Display or board driver**: the code lives in the `freeink-sdk` submodule
  ([SaiduthE/freeink-sdk](https://github.com/SaiduthE/freeink-sdk), branch `eminimal-7in8`). Open the pull
  request there, then a second one here that moves the submodule pointer.

## Getting set up

1. Read the [README](README.md) for the hardware and the build commands. You can develop on an
   ESP32-S3-DevKitC-1 (N16R8) wired to an IT8951 display board. Logic that does not touch hardware (game
   engines, parsers, stores) is covered by host unit tests and needs no device.
2. Read [docs/eminimal/](docs/eminimal/README.md). Start with the system overview and the application
   framework. The dashboard and games document has step-by-step guides for adding a tile or a game.
3. Read `CLAUDE.md` and `AGENTS.md` for the coding rules (memory use, rendering, logging). They apply to
   people as much as to coding agents.

## Making a change

1. Fork the repository and branch from `master`.
2. Keep a pull request to one topic. Small pull requests are reviewed faster.
3. Build the target and run the unit tests:

   ```sh
   pio run -e eminimal
   pio run -e eminimal -t unit-tests
   ```

4. Format C++ with the repository's clang-format configuration (`bin/clang-format-fix`, clang-format 21 or
   newer). CI checks formatting.
5. If the change affects behaviour described in `docs/eminimal/`, update the document in the same pull
   request.
6. Say in the pull request how you tested it: on a device, on a development board, or by unit test only.
   Changes to display refresh, sleep and power need a test on hardware. Say so if you could not do one.

## Reporting a bug

Open an issue with the firmware version (Settings, About), what you did, what you expected, what happened,
and a serial log if you have one (`pio device monitor -e eminimal`). For a book that renders wrongly, name
the book or attach a small sample if its licence allows.

## Security

Do not open a public issue for a security problem in the network features. Use GitHub's private
vulnerability reporting on this repository (Security tab, "Report a vulnerability").

## Licence of contributions

By contributing you agree that your contribution is licensed under the MIT licence of this repository.
Do not add code, fonts, images or word lists that you do not have the right to publish under it.
