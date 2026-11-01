---
description: "Task list for the Qt desktop frontend"
---

# Tasks: Qt desktop frontend

**Input**: `specs/003-qt-frontend/`: spec.md (clarified 2026-10-30),
plan.md, research.md, data-model.md, contracts/, quickstart.md.

**Tests**: included and written first in every phase.
- Assertions come from the spec, the contracts, hardware documentation or a
  test ROM's documented verdict, never from current output.
- GUI tests run offscreen (`QT_QPA_PLATFORM=offscreen`,
  `SDL_AUDIO_DRIVER=dummy`).

**Manual test**: every phase's PR carries its checklist from quickstart.md
(M0-M9). It merges only after the maintainer reports a pass, even when CI is
green.

**Organization**: one phase per PR, in order, each merged before the next
branch starts (`docs/source-control.md`). Issue #32 tracks the feature, and
every PR uses `Refs #32`. G9 uses `Fixes #32` only when every FR and SC is
met.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: can run in parallel (different files, no dependency on incomplete
  tasks)
- **[Story]**: US1-US9 from spec.md

---

## Phase G0: Build and CI (branch `feature/gui-skeleton`)

- [x] G001 Root `CMakeLists.txt`:
  - option `CUTENES_BUILD_GUI` (default ON when Qt 6.8+ is found, OFF
    otherwise, with a status message); when on, `add_subdirectory(qt)` and
    `add_subdirectory(app)`;
  - the core, api, mcp and server targets build without Qt as before.
- [x] G002 SDL 3.4.18 through pinned FetchContent (tag `release-3.4.18`):
  - a static build with only the audio, gamepad and joystick subsystems, and
    video, render and GPU off;
  - add NOTICE.md entries for SDL3 (zlib) and Qt (LGPL-3.0), and the license
    texts under `third_party/`.
- [x] G003 [P] `qt/CMakeLists.txt`: the `cutenes-qt` library (Qt6::Widgets,
  Qt6::Gui, SDL3 static, `cutenes-api`), with an export header and
  `include/cutenes/qt/` per `ui-embeddable-design.md`.
- [x] G004 [P] `app/CMakeLists.txt`: the `cutenes` executable, a
  `MACOSX_BUNDLE` with an Info.plist declaring the `.nes` document type, and
  `WIN32_EXECUTABLE`.
- [x] G005 [US3] Test first, `tests/gui/test_skeleton.cpp` (label `gui`):
  - the main window constructs offscreen;
  - its title is "cuteNES";
  - Help > About exists and shows the application, core and surface
    versions.
- [x] G006 [US3] `app/main.cpp` and `app/mainwindow.*`: an empty main window,
  a menu bar with the seven menus (empty except Help > About and File >
  Exit), and the About dialog with the license texts.
- [x] G007 CI (`.github/workflows/build.yml`):
  - `jurplel/install-qt-action@v4.4.1` installs Qt 6.8 (modules:
    qtshadertools) on all three OSes;
  - the path filter gains `qt/**` and `app/**`;
  - the fast groups gain `gui`;
  - the sanitize job builds without the GUI (`CUTENES_BUILD_GUI=OFF`) unless
    Qt is available there.
- [x] G008 README: build prerequisites for the GUI and how to run `cutenes`.
- [x] G009 Manual checklist **M0**.

## Phase G1: Play a game (branch `feature/gui-play`)

Real-time mode in `cutenes-api` (R3), test-first:
- [x] G010 [US1] Tests in `tests/api/test_realtime.cpp`:
  - a running session runs exactly one frame per requested frame (R3 as
    built: the host requests frames; there is no `IFrameClock`);
  - queued commands run between frames;
  - nothing runs while paused, and frames requested meanwhile are dropped;
  - live input reaches `core.get_controller` at the next frame start;
  - human and automation input are combined with OR;
  - a `core.run_frames` script replaces the automation part;
  - headless behavior is unchanged with no clock.
- [x] G011 [US1] Implement `Session::setRealtime`, `requestFrames`,
  `setLiveInput` and `setMediaSinks` (api/), per R3 as built, and document
  them in 002's `contracts/transports.md` ("GUI" mode). The surface version
  is unchanged: no command changes.

`cutenes-qt`:
- [x] G012 [P] [US1] Tests:
  - viewport math (scale, 8:7, 4:3, overscan, integer scaling) against
    hand-computed rectangles;
  - `InputMapper` defaults per the menus contract.
- [x] G013 [US1] `EmulationView`: a `QRhiWidget` with a texture upload of
  the newest frame (a triple buffer fed by the video sink), a nearest
  sampler, and the raster fallback when QRhi fails.
- [x] G014 [US1] `AudioOutput` (an SDL3 audio stream, with dynamic rate
  control per R4) and `FrameClock` (timer mode at the console rate), with
  tests: the frequency ratio stays within ±0.5%, and the clock ticks at its
  rate. The display-synced mode moves to G051.
- [x] G015 [US1] Keyboard input to `setLiveInput`, with keys cleared on focus
  loss.

`cutenes` app:
- [x] G016 [US1] `GuiHost` (`app/host/`): registers `gui.*` commands, with
  emulation-to-GUI-thread marshalling per R11. Every `QAction` carries its
  `command` property and triggers that command.
- [x] G017 [US1] Command line per `contracts/cli.md`: the ROM argument,
  `--help`, `--version` and `--fullscreen`.
- [x] G018 [US1] `gui.open_rom`, `gui.close_game`, `gui.toggle_pause`,
  `gui.reset`, `gui.power_cycle` and `gui.exit`, with menu items and default
  shortcuts. An unsupported ROM shows the core's reason and keeps the
  running game. Tests A1 (partial) and A7.
- [x] G019 Manual checklist **M1**.

## Phase G2: Library (branch `feature/gui-library`)

- [x] G020 [US2] Tests first:
  - `GameIdentity` against hand-built ROM images (headered, re-headered, with
    a trainer);
  - the file-name rules (sanitising, the `[id8]` lookup, a collision);
  - library layout creation (A2, A3).
- [x] G021 [US2] `app/library/`: `Library` (root, subfolders, README),
  `GameIdentity`, the index (JSON cache, `QtConcurrent` scan,
  `QFileSystemWatcher`) and scratch-load support status.
- [x] G022 [US1] Library browser: a model and view with name, mapper, region,
  support, save and state flags and last played; type-to-filter, sort,
  Recent, keyboard navigation, and an empty-library hint (US2-1).
  `gui.show_library`, `gui.library_select`, `gui.library_list`, and
  `session.library_list` mapped to it (FR-074).
- [x] G023 [US2] Battery saves:
  - tests (A4): the write within 10 s, atomic writes, and a corrupt save set
    aside;
  - implementation of the sram hash check, the writer thread, `QSaveFile`
    and the per-game `QLockFile`.
- [x] G024 [US2] Settings:
  - `QSettings` (INI);
  - the Settings > Library page (location change offers to move the
    contents);
  - `gui.get_settings` / `gui.set_setting` for the `library` group;
  - `--library DIR`.
- [x] G025 [US1] Open Recent and drag-and-drop. The macOS `QFileOpenEvent`
  is handled.
- [x] G026 Manual checklist **M2**.

## Phase G3: Menus, states, servers (branch `feature/gui-menus`)

- [x] G030 [US4] Tests first (A5): slot save and load give the identical
  hash, and Undo Load works.
- [x] G031 [US4] Slots with thumbnails, Select Slot, Quick Save and Load,
  Undo Load State, and states to and from files.
- [x] G032 [US4] Tests first (A6): rewind 120 frames lands on the matching
  hash, and delta and RLE round trips. Then rewind (hold), fast-forward
  (hold and toggle), speed, Frame Advance, and the audio policy during
  fast-forward. Measure state sizes and set the rewind budget so the default
  holds at least 30 s.
- [x] G033 [P] [US3] Screenshot (F12), the OSD message layer, Show FPS, and
  Open Library Folder. Import and export battery saves.
- [x] G034 [US3] The complete menu bar per `contracts/menus-and-commands.md`,
  with every item present. Items whose feature lands in a later phase are
  present but disabled, with the tooltip "Coming in a later update"; their
  commands return `NotImplemented` until then.
- [x] G035 [US6] Settings > Servers:
  - the API and "MCP server" toggles and ports, with MCP requiring the API;
  - the token file written for `--attach`, and a Copy Token button;
  - the status-bar automation indicator (FR-071-FR-073);
  - tests A9.
- [x] G036 [US3, US5] The Key Bindings page (#108), which replaces the
  separate Settings > Hotkeys and Settings > Input binding pages: every
  hotkey and the keyboard game controls of players 1 and 2, with rebind, a
  second binding, clear, reset one or all, conflict checks, persistence,
  menus that show the current bindings, and the `gui.*` binding commands,
  per the contract's "Key Bindings page". The gamepad column joins in G042.
- [ ] G037 Manual checklist **M3** (it includes attaching Claude Code).

## Phase G4: Input (branch `feature/gui-input`)

- [x] G040 [US5] Tests first:
  - binding conflicts;
  - turbo timing at 15 Hz;
  - opposite-direction resolution (last pressed wins);
  - gamepad mapping defaults from a fake device.
- [x] G041 [US5] SDL3 gamepads: polling, hot-plug, GUID-stable port
  assignment, and pause on disconnect.
- [x] G042 [US5] Gamepads on the Key Bindings page (G036): per-player
  gamepad bindings with press-to-bind, and gamepad buttons as hotkeys. Turbo
  rate and opposite directions on Settings > Input.
- [ ] G043 Manual checklist **M4**.

## Phase G5: Display and sound (branch `feature/gui-av`)

- [ ] G050 [US7] Tests first:
  - scale and aspect window sizes;
  - fullscreen state;
  - settings that persist across restarts;
  - WAV header and length (A-series).
- [ ] G051 [US7] Video settings (all but the pacing are in `feature/gui-video`;
  display-synced pacing goes with the audio work in `feature/gui-audio`): display-synced pacing (R4, moved from G1),
  scale, fullscreen with the menu reachable through Alt or the top edge, integer scaling, aspect, overscan, the smooth
  and scanline shaders, high DPI and Always on Top.
- [ ] G052 [US7] Audio settings: the device list, volume, mute, latency and
  device-change handling. Record Audio (WAV).
- [ ] G053 [US7] Pause when inactive, and resume where left off (the auto
  state).
- [ ] G054 Manual checklist **M5**.

## Phase G6: Core additions (branch `feature/core-region-palette-genie`)

Core rules apply: test first, with expectations from nesdev pages and
documented ROM behavior. `contracts/core-additions.md`.
- [ ] G060 Region override:
  - tests: a forced PAL frame has 312 lines and runs at the PAL master
    clock; the `pal_apu_tests` verdicts hold when forced;
  - implementation, `core.set_region_override`, and Emulation > Region.
- [ ] G061 Palette:
  - tests: the 64- and 512-entry outputs; frame hashes do not depend on the
    palette;
  - implementation and `core.set_palette`;
  - View > Palette with built-in palettes and `.pal` loading.
- [ ] G062 Game Genie:
  - tests: decoding the nesdev worked examples; substitution with and
    without compare, on a test image;
  - implementation, `core.set_game_genie`, `core.decode_game_genie`.
- [ ] G063 Core contract (`core-api.md`) amendments, capability rows, the
  golden catalog, the surface version MINOR bump, and the parity test.
- [ ] G064 Manual checklist **M6**.

## Phase G7: Cheats and movies (branch `feature/gui-cheats-movies`)

- [ ] G070 [US8] Tests first (A11, A12):
  - a movie from power-on and one from a state each replay to the same hash;
  - a movie for a different game is refused;
  - a read-only takeover truncates the movie;
  - cheat search on a test ROM finds a counter, by value, while the game
    runs (US8-2), with 1- and 2-byte values;
  - an edited value reaches the game at the next frame boundary, without a
    pause (US8-3, FR-053);
  - a freeze holds it.
- [ ] G071 [US8] Cheats window: freeze, set and Game Genie, stored per game,
  each switchable on and off. The cheat search window and its commands,
  usable while the game runs, with live values, in-place editing and a
  per-game watch list (FR-053).
- [ ] G072 [US8] Movies: record from power-on or a state, play, stop,
  read-only and takeover, the `.cnm` format, and the status bar.
- [ ] G073 Manual checklist **M7**.

## Phase G8: Debug tools (branch `feature/gui-debug`)

- [ ] G080 [US9] Tests first (A13):
  - a breakpoint at nestest's `$C000`;
  - 10 steps match nestest's log;
  - the memory viewer reads change no state;
  - the pattern table equals the CHR data.
- [ ] G081 [US9] Debugger: registers and flags, disassembly, step, run,
  breakpoints by stepping, and register edits.
- [ ] G082 [P] [US9] Memory viewer and editor: CPU, PPU, OAM and palette.
- [ ] G083 [P] [US9] PPU viewers: patterns, nametables with the scroll
  position, sprites and palette, refreshed at up to 30 Hz.
- [ ] G084 [US9] Trace logger and Write Memory Dump into `debug/`.
- [ ] G085 Manual checklist **M8**.

## Phase G9: Parity, acceptance, packaging (branch `feature/gui-release`)

- [ ] G090 [US6] The `gui-audit` test (A8): the menu actions, the contract
  table and the `gui.*` catalog are equal, and every action is wired.
- [ ] G091 [US6] `gui-coexistence` (A10, 002 SC-006) and the GUI-mode
  `--attach` test against the real application. This closes 002's GUI-mode
  items recorded in #32.
- [ ] G092 Pacing soak `gui-pacing` (A15) in Release.
- [ ] G093 Packaging per R13 (windeployqt, macdeployqt, AppImage with
  `.desktop`/MIME), with license files, on tags and weekly.
- [ ] G094 Docs: README "Using the GUI", a user guide reachable from Help, and
  a keyboard shortcut list from the contract.
- [ ] G096 [US10] Spec R16 settled: feed, asset names, checksums and the
  per-platform swap (#105). Release assets get the naming scheme (G093).
- [ ] G097 [US10] Tests first: a fake release feed for version comparison
  and asset choice, skip and later remembered, no prompt while a game runs,
  no request with checks off, a bad checksum replacing nothing, and the
  arguments carried to the relaunch.
- [ ] G098 [US10] The first-launch question, the setting, the background
  check and the prompt (FR-090 to FR-093, FR-096, FR-097).
- [ ] G099 [US10] Download, verify, replace and relaunch per platform
  (FR-094, FR-095), with a manual check on all three OSes.
- [ ] G095 Manual checklist **M9** (after G096-G099). If every FR and SC is met, the PR uses
  `Fixes #32`; otherwise it uses `Refs #32`, and issues are filed for the
  remainder.

## Dependencies

G0 → G1 → … → G9, strictly in order (one branch at a time). Inside a phase,
the tests come first and must fail before the implementation tasks start.
G6 may move earlier if the maintainer asks, because it touches only the
core and the API.
