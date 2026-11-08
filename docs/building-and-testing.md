# Building and testing

How to build cuteNES from source, run its tests, and what each test group
proves. For what a release contains and how packages are made, see
[`packaging.md`](packaging.md). For the test ROMs themselves, see
[`test-corpus.md`](test-corpus.md).

## Building

```bash
git clone --recurse-submodules https://github.com/tdjustus/cuteNES.git
cd cuteNES
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build -j8 -LE known-failing
```

Requires CMake >= 3.21 and a C++20 compiler (MSVC 2022+, AppleClang 15+, or
GCC 12+). `--recurse-submodules` fetches the test-ROM archive
(`tests/roms`). GoogleTest is fetched at configure time.

**The GUI** (`cutenes`) also needs **Qt 6.8 or later** with the Shader Tools
module. Install it with Homebrew (`brew install qt`), your Linux
distribution, or the Qt online installer. SDL3 is fetched automatically.

- When CMake can't find Qt, it builds everything except the GUI. Point it
  at a Qt installation with `-DCMAKE_PREFIX_PATH=<Qt dir>`, or pass
  `-DCUTENES_BUILD_GUI=ON` to make a missing Qt an error.
- On Linux, install the ALSA, PulseAudio and udev development headers first
  (`libasound2-dev libpulse-dev libudev-dev` on Debian and Ubuntu), so SDL3
  builds with real audio and gamepad support.

What the build produces:

| Path | What |
|---|---|
| `build/app/cutenes` (`cutenes.app` on macOS) | the desktop application |
| `build/server/cutenes-server` | the headless MCP and API server ([`automation.md`](automation.md)) |
| `build/tests/rom_harness` | the test-ROM harness (below) |
| the `cutenes-core` library | the emulation core ([`using-the-core.md`](using-the-core.md)) |

## Testing

Tests are labelled in CTest. Run one group with `ctest --test-dir build -L
<label>`. Counts are from the current tree.

| Label | Tests | What it proves | Runs in CI |
|---|---|---|---|
| `unit` | 219 | Component behavior written from hardware docs: CPU, PPU, APU, bus, scheduler, iNES parsing, each mapper's banking/mirroring/IRQ | every PR |
| `tier1` | 50 | The gate ROMs (nestest, instr_test-v5, instr_timing, ppu_vbl_nmi, apu_test, apu_reset, cpu_interrupts_v2, mmc3_irq_tests, ...) plus 13 golden frame hashes | every PR |
| `tier2` | 28 | Broader ROM suites incl. 22 Holy Mapperel board tests | every PR (Release builds) |
| `behavior` | 18 | Determinism (run twice, byte-identical), save-state round trip, instruction-step equivalence | every PR |
| `playtest`, `corpus` | 113 | Scripted ROM runs with on-screen text or pinned, hand-viewed frame hashes | every PR, except entries needing `roms-local/` |
| `api` | 104 | The `cutenes-api` command layer and HTTP API (feature 002): every `core.*`/`session.*` command against the contracts, a test ROM's own verdict through the commands, save-slot branching, structured errors that change no state, call ordering across threads and clients, JSON-RPC 2.0 framing, the token, Host and Origin checks, the frozen catalog, a GUI host's `gui.*` commands reaching both catalogs and MCP clients' list-changed notifications, and real-time play (one frame per requested frame, commands between frames, human and automation input combined, a host frame runner) | every PR |
| `mcp` | 39 | The MCP adapter (feature 002) against MCP's own pinned `schema.json` for both revisions it serves (2026-07-28 and 2025-11-25): discovery over stdio with the real executable, a test ROM's verdict and a PNG image block through tool calls, `tools/list` equal to `api.catalog`, Streamable HTTP's header and status rules, progress and subscription streams, cancellation when a client goes away, and the `--attach` bridge relaying a stdio client to a running instance, waiting for one that starts later (for clients of either revision) and following it across restarts | every PR |
| `gui` | 277 | The Qt frontend (feature 003), offscreen and with SDL's dummy audio: the window and its seven menus in the spec's order, every contract item in the menus and the catalog (later ones disabled, returning `NotImplemented`), the menu actions' `gui.*` commands, shortcuts and platform roles, About's versions and license texts (Qt's LGPL included), SDL starting audio and gamepads, SIGTERM still ending the process, picture sizing (aspect, overscan, integer scaling), keyboard defaults, the frame hand-off, the frame clock, audio rate control within ±0.5%, the command line (its options, `--load-state` loading after the boot, and `--api`/`--mcp` leaving the saved settings alone), playing a test ROM to its own "passed" in real time with the view showing the core's exact frame, game identity and file naming, the library layout, index and browser, recent games, Settings > Library, rewind (a rewound game in exactly the state a fresh run reaches, the history kept within budget, 30 s in far less than 64 MiB), fast-forward, speed and Frame Advance, a top-right indicator for rewind, fast rewind and speed in the style of PAUSED at half its size, save-state slots (the same hash after a load, a load waiting paused and then playing on with sound, a name asked for and a warning before replacing, states kept across a ROM rename, "PAUSED" over a paused game, messages that never resize the view, thumbnails, the selected slot per game, one level of undo, state files, another board's state refused), the pause menu (Resume, Save, Battery Save and Quit after Pause, only Resume after any other pause, Save asking as Quick Save does, Battery Save written at once, Quit asking first while a client's close never does, no frame run while it shows, and the keyboard and a gamepad moving between the buttons), screenshots (the core's exact frame), Show FPS, on-screen messages that can be turned off, battery save import and export, Settings > Servers (off by default, MCP turning the API on, the token files `--attach` reads, a client turning the API off through itself, the automation indicator and an automation pause, Disable Automation cutting a client off mid-call, the servers lasting one run unless the user confirms they start with cuteNES, and automation unable to turn that on), and battery saves (written within the interval and on close, atomic, found by identity after a rename, a bad save set aside, one writer per game) | every PR (not the sanitizer job) |
| `known-failing` | 0 | Documented hardware discrepancies. None at present: the last one, `vbl_nmi_timing/7` (#42), passes since the `$2002` status-read redesign | never (run with `ctest -L known-failing`) |

What the numbers do not say: a golden hash records what this emulator draws,
not that it is right. Each of the 13 goldens carries a status (verified,
checked by eye, or unverified) in `tests/golden/roms.txt`. Commercial-game
playtests need ROMs you supply in the gitignored `roms-local/` folder and
skip otherwise. Open core issues are listed in the
[1.0 milestone](https://github.com/tdjustus/cuteNES/milestone/1). The
corpus, how each folder is used and the pinned-hash procedure are in
[`test-corpus.md`](test-corpus.md).

### Where test expectations come from

A test's expected value comes from hardware documentation (the
[NESdev wiki](https://www.nesdev.org/wiki/NES_reference_guide)) or from a
test ROM's own documented verdict, never from what the emulator happens to
output today. A test that only checks "the screen changed" proves very
little. Core behavior changes are test-first: write the failing test, then
the fix. These rules are in the project's constitution
(`.specify/memory/constitution.md`).

## CI

CI (`.github/workflows/build.yml`) runs on pull requests, on `v*` and
`core-v*` tags, weekly, and on manual dispatch:

- Release builds on Linux (GCC), macOS (AppleClang) and Windows (MSVC);
- an MSVC Debug build with checked iterators;
- an ASan/UBSan build with libstdc++ assertions (the fast groups on pull
  requests, everything on tags and weekly);
- a speed gate on the Linux job. A development machine runs the core at
  roughly 7x real time; CI fails below 1x.

Docs-only changes skip the build. The single required check is `ci-ok`,
which passes only when every job above succeeded or was skipped. The
packages are built on tags, weekly and on manual dispatch, not on pull
requests. Why, and the rest of the CI policy, are in
[`source-control.md`](source-control.md).

## The ROM harness

```bash
build/tests/rom_harness suite --rom <rom>             # blargg $6000 protocol
build/tests/rom_harness playtest --list FILE [--entry NAME] [--shots DIR]
build/tests/rom_harness shot ROM FRAMES out.ppm       # save a frame to look at
build/tests/rom_harness determinism --rom R --frames N
build/tests/rom_harness savestate --rom R --at F --rerun-to G
build/tests/rom_harness step-equiv R N                # stepping == runFrames
build/tests/rom_harness bench --rom R [--seconds N] [--min-x X]
build/tests/rom_harness golden --list tests/golden/roms.txt --check tests/golden/baseline.txt
```
