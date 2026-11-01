# Implementation plan: Qt desktop frontend

**Branch**: per-phase branches (see tasks.md) | **Date**: 2026-10-31 | **Spec**: [spec.md](spec.md) | **Issue**: #32

**Input**: Feature specification from `specs/003-qt-frontend/spec.md`,
clarified 2026-10-30.

## Summary

Build `cutenes`, a Qt 6 desktop application. It hosts the same `cutenes-api`
**Session** that the headless server uses, so the GUI, the API and MCP drive
one machine through one command queue (spec FR-075, 002 FR-007).

1. **`cutenes-api` gains a real-time mode** (R3). While the machine is
   running, the session's emulation thread runs frames on a clock that the
   host supplies. It handles queued commands between frames and merges live
   human input with automation input at each frame start. Headless mode is
   unchanged.
2. **`cutenes-qt`** is the embeddable library planned in
   `ui-embeddable-design.md`. It contains:
   - `EmulationView`, which renders through `QRhiWidget`, with a raster
     fallback (R2);
   - the audio output and the frame clock (SDL3 audio stream with dynamic
     rate control, R4);
   - keyboard and gamepad input (SDL3 gamepads, R5).

   It uses only the public core and API headers.
3. **`cutenes`**, the application, contains:
   - the main window and the complete menu bar
     ([contracts/menus-and-commands.md](contracts/menus-and-commands.md));
   - the library and its browser
     ([contracts/library-layout.md](contracts/library-layout.md));
   - the settings, dialogs, debug views, cheats and movies;
   - the `GuiHost`, which registers one `gui.*` command per menu action and
     setting (R11);
   - the API and MCP listeners from feature 002 behind the Servers settings.
4. **Core additions** (feature 001 amendments, test-first): region override,
   palette selection and Game Genie (R10).

The phases are ordered so that each PR gives the maintainer something to
launch and test by hand (the manual test checklists are in
[quickstart.md](quickstart.md)).

Decisions and sources: [research.md](research.md).

## Technical context

**Language/version**: C++20, with the same toolchains as the core (MSVC
2022+, AppleClang 15+, GCC 12+).

**Primary dependencies**:
- **Qt 6.8 or later** (LTS): modules Widgets, Gui (`QRhiWidget`), Concurrent
  and ShaderTools. It is linked dynamically under LGPL-3.0 (R1).
- **SDL 3.4.18** (zlib): audio output and gamepads, linked statically through
  FetchContent (R4, R5).
- Existing: `cutenes-api` and `cutenes-mcp` (nlohmann/json, cpp-httplib).

**Storage**: files only.
- The library folder holds ROMs, saves, states, screenshots, movies,
  recordings, cheats and debug output
  ([contracts/library-layout.md](contracts/library-layout.md)).
- Settings are stored with `QSettings` as an INI file in the per-user config
  location.
- The library index is a JSON cache in the per-user cache location.

**Testing**:
- Qt Test and GoogleTest, under the new ctest labels `gui` and `gui-audit`.
- They run with `QT_QPA_PLATFORM=offscreen` and SDL's dummy audio driver, so
  CI needs no display or sound device.
- The `api` tests cover the real-time mode.
- The core additions add `unit`/`tier1` tests.
- Each phase also has a **manual test checklist** that the maintainer runs
  before its PR merges.

**Target platform**: Windows 10+, macOS 13+ and Linux (X11 and Wayland)
(constitution IV).

**Project type**: one library (`cutenes-qt`) and one desktop application
(`cutenes`), plus small additions to `cutenes-api` and `cutenes-core`.

**Performance goals**:
- Native frame rate with no audio underruns (SC-004).
- Input within one frame (SC-005).
- The library browser usable within 1 s with 2,000 ROMs (SC-002).
- Debug views refreshing at 30 Hz without slowing emulation below full speed.

**Constraints**:
- Only the session's emulation thread touches the `Machine`.
- The GUI thread never blocks on emulation for longer than one command.
- Determinism holds: movies and states replay identically.
- Off by default: no network port opens unless the user turns a server on.
- No outbound request except the opt-in update check (US10, R16).

**Scale/scope**:
- About 120 `gui.*` commands (one per menu action and setting).
- 7 menus and 9 dialogs or tool windows.
- 3 core additions.

## Constitution check

| Principle | How this plan satisfies it | Verdict |
|---|---|---|
| I. Hardware fidelity | The core additions cite their hardware sources: the nesdev wiki "Game Genie" page, "PPU palettes" and "Clock rate" for the region timing. The GUI changes no emulation behavior | PASS |
| II. Core/UI separation | `cutenes-core` gets no Qt or SDL dependency. The three additions are plain C++ API extensions. `cutenes-qt` and `cutenes` use only public headers. The embeddable view keeps the design in `ui-embeddable-design.md` (FR-081) | PASS |
| III. Test ROMs are the gate | The core additions are test-first (unit tests plus a Game Genie test ROM scenario). The whole suite still gates every PR | PASS |
| IV. Cross-platform | Qt 6 is the only UI toolkit. Platform code (audio, gamepads, paths) sits behind `cutenes-qt` interfaces and SDL3. CI builds and runs the `gui` tests on all three OSes | PASS |
| V. Automation parity | Every menu action and setting is a `gui.*` command (FR-070). The `gui-audit` test proves the menu and the catalog map 1:1 (SC-003). The core additions get `core.*` commands. Human and AI share one queue | PASS |
| Constraints: licensing | Qt is LGPL-3.0, dynamically linked, with notices in About and in the packages. SDL3 is zlib. Both are recorded in `third_party/NOTICE.md`. `qsb` (GPL-3.0) runs only at build time, and its output is not covered (R1) | PASS |
| Constraints: performance | Pacing is measured in a `gui` test: frame-time histogram and audio queue underruns over 10 minutes (SC-004). The core benchmark gate is unchanged | PASS |

**Post-design re-check.** The contracts add three core functions (R10), one
`cutenes-api` real-time interface (R3) and the `gui.*` catalog. No
emulation behavior changes outside the three amendments. **PASS.**

## Project structure

### Documentation

```text
specs/003-qt-frontend/
├── spec.md                       # clarified 2026-10-30
├── plan.md                       # this file
├── research.md                   # R1-R15
├── data-model.md
├── quickstart.md                 # automated scenarios + manual checklists
├── contracts/
│   ├── menus-and-commands.md     # every menu item -> gui.* command -> shortcut
│   ├── library-layout.md         # the library folder, file names and formats
│   ├── cli.md                    # cutenes command line
│   └── core-additions.md         # region, palette, Game Genie (001 amendment)
└── tasks.md                      # /speckit-tasks
```

### Source

```text
core/                             # + setRegionOverride, setPalette, Game Genie (R10)
api/                              # + real-time mode: IFrameClock, live input (R3)
qt/                               # cutenes-qt: the embeddable library
├── include/cutenes/qt/           # EmulationView, AudioOutput, FrameClock, InputMapper
├── src/
│   └── shaders/                  # nearest/smooth/scanline (qt_add_shaders)
└── CMakeLists.txt
app/                              # cutenes: the desktop application
├── main.cpp                      # CLI (contracts/cli.md)
├── mainwindow.*                  # menus, status bar, OSD
├── library/                      # Library, index, browser model/view, GameId
├── persistence/                  # battery saves, states, rewind, settings
├── dialogs/                      # settings pages, cheats, movie, about
├── debug/                        # debugger, memory, PPU, trace views
├── host/                         # GuiHost: gui.* commands, thread marshalling
└── resources/                    # icons, default palettes, licenses
tests/gui/                        # Qt Test: offscreen app, menu audit, pacing
third_party/                      # NOTICE.md gains Qt, SDL3
```

**Structure decision.** `qt/` is the reusable library and `app/` is the
product, as in `ui-embeddable-design.md`. The link direction is `core` <-
`api` <- `qt` <- `app`, plus `mcp` <- `app`. The tree shows it, and the link
dependencies enforce it. `cutenes-server` stays Qt-free.

## Phasing (one branch and PR each, merged before the next)

Every phase ends with the automated tests green on all three OSes **and the
maintainer's manual test passing** (quickstart.md "Manual checklists").

| Phase | Delivers | Exit |
|---|---|---|
| G0 | Build and CI: Qt and SDL3 found or fetched, `qt/` and `app/` skeletons, CI jobs that install Qt 6.8 on all three OSes, the `gui` label, NOTICE entries | An empty window opens on all three OSes; CI green |
| G1 | Play a game: the API real-time mode (R3), `EmulationView` (QRhi with raster fallback), audio and pacing (R4), keyboard input, `cutenes ROM` from the CLI, File > Open ROM, Pause/Reset/Power, the first `gui.*` commands | M1: launch a ROM and play it at full speed with sound |
| G2 | Library: the folder layout, the scan and index, the browser (mouse, keyboard, gamepad focus), Recent, battery saves (atomic writes, 10 s flush), Settings > Library, persisted settings | M2: browse, launch, save and resume a battery game |
| G3 | The complete menu bar: states and slots with thumbnails, Undo Load, rewind, fast-forward/speed, frame advance, screenshots, OSD, hotkeys, Servers settings (API, MCP server, token), status indicator | M3: every menu item works; an MCP client attaches |
| G4 | Input: SDL3 gamepads, the remapping dialog, turbo, hot-plug pause, opposite directions, the hotkey dialog | M4: play with a gamepad; remap |
| G5 | Display and sound: scale, fullscreen, integer scaling, aspect, overscan, filters, high DPI, audio device, latency and volume, WAV recording, pause when inactive, resume-where-left-off | M5: every display and sound option |
| G6 | Core additions (R10), test-first in `core/`, with `core.*` commands, then Emulation > Region and View > Palette | M6: a PAL override, a `.pal` palette |
| G7 | Cheats (search, freeze/set, Game Genie) and movies (record, play, read-only, takeover) | M7: find and freeze a counter; record and replay a movie |
| G8 | Debug tools: debugger with breakpoints, memory viewer/editor, PPU viewers, trace logger, memory dump | M8: break at `$C000` in nestest; inspect the PPU |
| G9 | Parity and acceptance: the `gui-audit` 1:1 test, 002 GUI-mode acceptance (US5, SC-006), the pacing soak test, packaging (windeployqt, macdeployqt, Linux AppImage) with license notices, the README and user guide | M9: full walkthrough from a packaged build |

G6 changes the core. Its PR is test-first, follows the core's rules
(CLAUDE.md), and runs the complete ROM suite.

## Complexity tracking

| Item | Why it is needed | Simpler alternative rejected because |
|---|---|---|
| SDL3 as a dependency | Qt 6 has no gamepad module. Qt Multimedia's audio sink has no rate control, so dynamic rate control would need a resampler of our own. SDL3's audio stream has one built in (`SDL_SetAudioStreamFrequencyRatio`) | Qt Multimedia without gamepads fails US5. Per-OS gamepad code violates constitution IV's "behind interfaces" goal at a much higher cost |
| QRhi rendering with a raster fallback | Vsync-locked presentation without tearing on all three OSes, and shader filters | Raster alone tears on Windows and can't sync to the display (SC-004). OpenGL alone is deprecated on macOS |
| A real-time mode in `cutenes-api` | The GUI, the API and MCP must share one queue and one machine (FR-075) | A GUI-owned machine would split the queue and break 002 SC-006 |
| Execution breakpoints by stepping | The core has no break hook. Stepping instructions while breakpoints are set is exact | A core break hook is a core API change that no feature needs yet |
