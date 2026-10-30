## e-Minimal fork

This checkout is the **e-Minimal** fork of CrossPoint: a DIY ESP32-S3 reader
with a 7.8" Waveshare IT8951E panel, not the Xteink X4 `AGENTS.md` describes.
`AGENTS.md` stays upstream-pristine (it rebases against
crosspoint-reader/crosspoint-reader) and is not imported here; the rules
below are the parts of it that apply to this fork. Open `AGENTS.md` only
for upstream detail not covered here.

## Coding rules (from AGENTS.md)

- **Evidence.** Cite file:line for proposed changes. Don't assume an API
  exists; check `freeink-sdk` or https://freeink.org/llms.txt. Explain the
  mechanism behind any claimed memory or speed gain, and tell the user how
  to verify a fix (serial log, heap, cache file).
- **Allocation.** No bare `new` (with `-fno-exceptions` OOM calls `abort()`):
  use `makeUniqueNoThrow<T>()` (`lib/Memory/Memory.h`), or `new
  (std::nothrow)`/`malloc` only when a C API takes ownership. Null-check and
  `LOG_ERR` on failure. Justify each new heap allocation; `reserve()` before
  `push_back` loops; allocate in `onEnter()`, free in `onExit()`, no
  new/delete in loops. Keep stack locals < 256 bytes.
- **Constants/strings.** `constexpr` (`static constexpr` in classes) for
  constants and tables. No `std::string`/Arduino `String` in hot paths; use
  `string_view` + `snprintf` into `char[]`. A `string_view` is not
  null-terminated: convert before any C API (`drawText`, `snprintf`, paths).
- **Avoid bloat.** No `std::function` or needless templates in library or
  render-loop code; use function pointers / `{void* ctx; void (*fn)(void*)}`.
- **Text and logs.** User-facing text goes through `tr(STR_*)`; logging uses
  `LOG_ERR`/`LOG_INF`/`LOG_DBG` (hardcoded strings fine), never raw Serial.
- **Errors.** No exceptions or `abort()`. Pattern: `LOG_ERR` + `return
  false`, or `LOG_ERR` + fallback; `assert` only for impossible states.
- **SD access.** Only through `HalStorage` (`Storage`) and `HalFile`; never
  SdFat/`FsFile`/`SDCardManager` directly (SdFat isn't thread-safe; the HAL
  mutex prevents a FreeRTOS assert panic). `DESTRUCTOR_CLOSES_FILE=1`: don't
  `close()` locals, except before `Storage.remove()` or reopening the same
  path; close member files in `onExit()`. Debounce `/.crosspoint/` writes.
- **Activities/tasks.** Activities are heap-allocated and deleted on exit.
  `vTaskDelete()` tasks in `onExit()` before destruction; free in reverse
  order. Task stacks (bytes): 2048 rendering, 4096 network/EPUB parsing.
- **ISR safety.** ISRs `IRAM_ATTR`, data they read `DRAM_ATTR`; no mutex in
  an ISR (use `...FromISR` + `portYIELD_FROM_ISR`). Read unaligned
  multi-byte values with `memcpy`, never a pointer cast.
- **UI.** Render through `GUI` (UITheme), no hardcoded fonts/positions; use
  `renderer.getScreenWidth()/getScreenHeight()` and
  `getOrientedViewableTRBL()`; check orientation switch coverage (4 modes).
- **Style.** PascalCase classes and file names, camelCase members (no
  prefix), UPPER_SNAKE constants, `#pragma once`. Short comments written
  for the merged state; no before/after narration.
- **Generated files** (never edit or commit): `src/network/html/*.generated.h`
  (source `src/network/html/*.html`, built by `scripts/build_html.py`) and
  `lib/I18n/I18nKeys.h`/`I18nStrings.{h,cpp}` (source
  `lib/I18n/translations/*.yaml`, `english.yaml` is the reference; regen with
  `python scripts/gen_i18n.py lib/I18n/translations lib/I18n/`).
- **Cache versions.** Bump `BOOK_CACHE_VERSION`
  (`lib/Epub/Epub/BookMetadataCache.cpp`) or `SECTION_FILE_VERSION`
  (`lib/Epub/Epub/Section.cpp`) before changing a binary layout, and update
  `docs/file-formats.md`.
- **Build/verify.** Build once after the last code edit (`pio run -e
  eminimal`); no clean by default, no rebuild for docs/comment-only changes.
  Flag device, orientation, heap and cache-reparse checks for the user.
- **Git.** Commit only when asked; never push or open/close a PR without
  approval. Messages `<type>: <summary>` (feat, fix, refactor, docs, test,
  chore, perf). No Claude/assistant co-author or generated-by trailers; add
  the original human author as `Co-Authored-By` when adapting their PR. Check
  `git status` against `.gitignore` before staging.
- **Scope.** A dedicated e-reader, not a Swiss Army knife: features that add
  RAM pressure without improving reading are out of scope.

## e-Minimal specifics

**Board.** Freenove ESP32-S3 devkit, N16R8 module (16 MB flash, 8 MB octal
PSRAM), `qio_opi` memory type (not `dio_opi`). Panel: IT8951 1872x1404,
`uiScale` 1.5 (the UI fonts are the 2x tier — Ubuntu 20/24, NotoSans 16
small; only the 12 px hint font is 1.5x; see `docs/web-ui.md` and
`e-Minimal-Notes/tools/ui_tune.md`).

**Five buttons, not seven.** No side buttons: four front keys in a row under
the screen — Back (GPIO 7), Confirm/Select (GPIO 6), Up (GPIO 4), Down
(GPIO 5) — plus Power (GPIO 15). `Button::Left`/`Button::Right` map to
`PIN_UNASSIGNED` here and never fire. The four front keys are user-remappable
(Settings › Controls › Remap Front Buttons, stored as `buttonMap*`, resolved
in `MappedInputManager::mapButton()`); page-turn direction is "Page Turn
Buttons" (the `sideButtonLayout` field); "Orient Front Buttons" turns Up/Down
with the reader's rotation. Detect this board at runtime with
`five_button::active()` (`src/util/FiveButtonInput.h`); **never** branch on
`Button::Left`/`Button::Right` reaching the user, and read keys through
`MappedInputManager`, not `HalGPIO::BTN_*`, unless a site must stay physical.
Pin table: `e-Minimal-Notes/MAP.md` §2; controls: `docs/eminimal-controls.md`.

**Branch.** Work directly on `eminimal-7in8` in the main checkout
(`D:\Personal_Projects\e-Minimal\e-Minimal`); the SDK submodule's branch is
`eminimal-7in8` too. No feature branches or worktrees unless Sai asks for one.

**Build env.** Always pass `-e eminimal`: `platformio.ini:2`
`default_envs = default` is the Xteink C3 target; on a clone without
`platformio.local.ini` (which pins `default_envs = eminimal` on this PC), a
bare `pio run` builds the wrong chip.

**Console/flash ports.** COM5 = native USB (USB-Serial-JTAG): console,
flashing, `CMD:` commands. COM4 = CH343 UART0, ROM banner only.
`platformio.local.ini` (gitignored, new) pins `upload_port`/`monitor_port`
to COM5 so `pio run -e eminimal` needs no `--upload-port`.

**Sandboxed `pio` hangs.** `pio run` hangs at SCons startup under a
sandboxed Bash tool; run build/flash with the sandbox off or hand them to
the user. `e-Minimal-Notes/tools/dev.ps1` wraps build/flash/monitor/
capture/size for this env and port — see `MAP.md` §3.

**clang-format.** Needs clang-format >= 21, not installed on this PC;
`.\bin\clang-format-fix.ps1 -g` fails here. Hand-match 120 cols / 2-space
indent; CI's format check is the backstop. Don't set `core.hooksPath
.githooks` until clang-format 21 is on PATH.

**Firmware size.** `python scripts/firmware_size_history.py --env eminimal
--commits <refs>` — the script defaults to env `default`, the wrong chip.

Read `e-Minimal-Notes/MAP.md` (navigation) and `e-Minimal-Notes/STATUS.md`
(current state) before non-trivial work.

**Submodule order.** `freeink-sdk` is a real submodule: commit and push it
first, bump and commit the fork's gitlink, then push the fork. Never run
`git submodule update` while `freeink-sdk` has commits the fork's gitlink
does not record yet: it detaches the submodule HEAD at the recorded SHA and
those commits vanish from the checkout (recoverable only from the SDK
branch or reflog).

**Settings enums are append-only.** `CrossPointSettings.h` enums and their
`SettingsList.h` label arrays are persisted by index in `settings.json`;
only append new values at the end, never insert or reorder.

**Where AGENTS.md is wrong for this fork:** no `data/html/` (HTML source is
`src/network/html/*.html`); its hardware section, 7-button mapping and
`clang-format-fix` step describe the Xteink X4 setup, superseded above.
