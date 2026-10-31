# Quickstart and validation: Qt desktop frontend (feature 003)

## Build and run

Qt 6.8 or later and CMake are needed. SDL3 is fetched at configure time.

```bash
# macOS (Homebrew Qt):   brew install qt
# Linux:                 Qt 6.8+ from your distribution or the Qt installer
# Windows:               the Qt online installer (MSVC 2022 64-bit kit)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release   # add -DCMAKE_PREFIX_PATH=<Qt dir> if not found
cmake --build build -j
ctest --test-dir build -j8 -LE known-failing      # includes the gui labels
```

The build produces `build/app/cutenes`; on macOS it is `build/app/cutenes.app`
(run `open build/app/cutenes.app --args <ROM>`).

The free test ROMs in `tests/roms/` are enough for every check below. In
the manual checklists, `nestest`, `01-basics` and `holy-mapperel` refer to
files under `tests/roms/`. A battery-backed game from `roms-local/` makes
the save checks more realistic.

## Automated scenarios (ctest `gui`, `gui-audit`, `api`, `unit`)

| # | Scenario | Spec | Test |
|---|---|---|---|
| A1 | Launch with no ROM shows the library. Launch with a ROM boots it. An invalid ROM shows the library plus an error | US1, FR-001, FR-006 | `gui-launch` |
| A2 | The library scan lists every `.nes` under `roms/` (nested) with header info and support status. A file added or removed is reflected without a restart | FR-002, FR-005 | `gui-library` |
| A3 | Renaming or moving a ROM keeps its battery save and states; two headers with the same content share saves | FR-011 | `gui-identity` |
| A4 | A battery game that writes RAM has its save on disk within 10 s. A killed process leaves an intact save (atomic writes) | FR-012, SC-006 | `gui-battery` |
| A5 | Save slot N, run on, load slot N: same frame hash. Undo Load returns to the pre-load hash | FR-033, SC-007 | `gui-states` |
| A6 | Rewind for 120 frames lands on the state recorded 120 frames earlier (same hash) | FR-034 | `gui-rewind` |
| A7 | Live input reaches `core.get_controller` at the next frame. Human input and automation input combine with OR | FR-031, R3 | `api-realtime`, `gui-input` |
| A8 | The menu audit: the menu actions, this contract's table and the `gui.*` catalog are equal, with shortcuts shown and game-only actions disabled without a game | FR-020, FR-021, FR-070, SC-003 | `gui-audit` |
| A9 | Servers: off by default, no port open. MCP on starts the API and writes the token file, and `cutenes-server --attach` lists `gui.*`. API off returns `ShuttingDown` and play continues | FR-071-FR-073, 002 FR-009/FR-020 | `gui-servers` |
| A10 | Interleaved GUI and automation state changes (input, save/load, reset, memory writes) never show a torn state | FR-075, SC-008 | `gui-coexistence` |
| A11 | A movie recorded from power-on replays to the identical final hash; a movie for another game is refused | FR-052, SC-007 | `gui-movie` |
| A12 | Cheat search finds a counter in a test ROM; a freeze cheat holds it | FR-050, FR-051 | `gui-cheats` |
| A13 | Breakpoint at `$C000` in nestest stops there; stepping matches nestest's log | FR-060 | `gui-debugger` |
| A14 | The core additions: region override timing, palette output, Game Genie decoding and substitution | R10 | `unit`, `tier1` |
| A15 | Pacing soak with a simulated clock: 36,000 frames, no underrun, no dropped or repeated frame | FR-030, SC-004 | `gui-pacing` (Release) |

## Manual checklists (the maintainer, before each phase merges)

Each PR description repeats its checklist. Report "passed" or what went wrong
in the PR. The PR merges only after a pass (feedback 2026-10-31).

**M0 (G0): skeleton**
- [ ] `build/app/cutenes` opens an empty window titled "cuteNES" on your OS.
- [ ] The window resizes and closes normally, and Help > About opens.

**M1 (G1): play a game**
- [ ] `cutenes tests/roms/instr_test-v5/rom_singles/01-basics.nes` shows "Passed" (a
      game you own is a better feel test).
- [ ] The picture is sharp at 3x with 8:7 proportions. The sound is clean,
      with no crackle.
- [ ] Arrows, Z, X, Enter and Right Shift control the game, and input feels
      immediate.
- [ ] P pauses and resumes, Ctrl+R resets, and Ctrl+Shift+R power-cycles.
- [ ] File > Open ROM opens a ROM from any folder.

**M2 (G2): library**
- [ ] With no argument, the library appears. The first launch created
      `~/Documents/cuteNES` with its subfolders.
- [ ] Copy ROMs into `roms/`: they appear without a restart.
- [ ] Filter by typing, launch with Enter, and get back with Ctrl+Shift+O.
- [ ] A battery game saves on close and resumes its save when reopened, also
      after you rename the ROM.

**M3 (G3): menus, states, servers**
- [ ] Every menu item works, and each shortcut is shown next to its item.
- [ ] Shift+F1 saves and F1 loads. Slot menus show thumbnails, and Ctrl+Z
      undoes a load.
- [ ] Holding Backspace rewinds, holding Tab fast-forwards, and \\ advances
      one frame while paused.
- [ ] F12 saves a screenshot into `screenshots/`.
- [ ] Settings > Servers: turn on "MCP server" and attach Claude Code via
      `cutenes-server --attach`. The status-bar indicator appears, and the AI
      can pause and play.

**M4 (G4): input**
- [ ] A gamepad works without setup. Unplugging it pauses the game.
- [ ] Remapping a button by pressing a key works, and a conflict is shown.
- [ ] Turbo A and Turbo B work.

**M5 (G5): display and sound**
- [ ] Try every scale, fullscreen (F11 and back), integer scaling, each
      aspect ratio, overscan and filter.
- [ ] Volume and mute work, and changing the audio device mid-game keeps
      playing.
- [ ] Losing focus pauses the game (when on). Record Audio writes a WAV.

**M6 (G6): core additions**
- [ ] Emulation > Region > PAL makes a game run at 50 fps.
- [ ] View > Palette > Load .pal changes the colours.

**M7 (G7): cheats and movies**
- [ ] Cheat Search finds lives or a timer; freezing it holds it. A Game
      Genie code you know works.
- [ ] Record a short movie, then play it back identically. Taking over from
      read-only continues recording.

**M8 (G8): debugging**
- [ ] A breakpoint at the reset vector stops at power-on. Step, run, and
      edit a register.
- [ ] The memory viewer edits RAM live, and the PPU viewers update while
      playing.
- [ ] The trace logger writes a file, and Write Memory Dump names its file.

**M9 (G9): release walkthrough**
- [ ] Install the packaged build for your OS. About lists the licenses, and
      the `.nes` association opens a game.
- [ ] Play a session from launch to quit, using the library, states, a
      gamepad and the MCP server.
