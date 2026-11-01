# cuteNES

[![build](https://github.com/tdjustus/cuteNES/actions/workflows/build.yml/badge.svg)](https://github.com/tdjustus/cuteNES/actions/workflows/build.yml)

An open-source, cross-platform NES emulator built on a hardware-accurate,
headless, deterministic emulation core, with an MCP server so AI agents can
connect to and drive the emulator.

- **Core** (`core/`): native C++20, no runtime dependencies, deterministic
  (the same ROM and inputs give byte-identical video and audio on every
  platform), validated against community test-ROM suites.
- **Server** (`cutenes-server`): the core behind MCP, for AI agents (stdio by
  default, or Streamable HTTP), and a local JSON-RPC API for scripts
  ([`specs/002-ai-mcp-server`](specs/002-ai-mcp-server)). See "Using the
  server".
- **Frontend** (`cutenes`, in progress): a Qt 6 desktop app
  ([`specs/003-qt-frontend`](specs/003-qt-frontend)). So far it is a window
  with its menus; play arrives in the next phase.
- **Normative hardware reference**: the
  [NESdev NES Reference Guide](https://www.nesdev.org/wiki/NES_reference_guide).

## Status

The emulation core ([spec 001](specs/001-emulation-core/spec.md)) is
feature-complete for the mappers below and is hardened toward version 1.0.
What is left is tracked in the GitHub
[1.0 milestone](https://github.com/tdjustus/cuteNES/milestone/1): a small
number of timing edge cases, the Qt frontend (including the MCP server in GUI
mode) and a second MCP client check. See
"Testing" for what has actually been verified.

| | Supported |
|---|---|
| CPU/PPU/APU | Cycle-stepped 6502 including unofficial opcodes, dot-stepped PPU, APU with DMC DMA |
| Regions | NTSC and PAL (PAL is validated per frame and for the APU, not per PPU dot) |
| Mappers | 0 NROM, 1/155 MMC1 (incl. SUROM/SXROM), 2 UxROM, 3 CNROM, 4 MMC3, 5 MMC5 (no expansion audio), 7 AxROM, 11 Color Dreams, 22 VRC2a, 28 Action 53, 34 BNROM |
| Headers | iNES 1.0 and NES 2.0 (RAM sizes, TV system, submappers) |
| Save states | Byte-identical round trip |

Revision-dependent mapper behavior (MMC1A/B, MMC3 A/B) is documented in
[`docs/mappers.md`](docs/mappers.md).

## Building

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build -j8 -LE known-failing
```

Requires CMake >= 3.21 and a C++20 compiler (MSVC 2022+, AppleClang 15+, or
GCC 12+).

The GUI (`cutenes`) also needs **Qt 6.8 or later** with the Shader Tools
module. Install it with Homebrew (`brew install qt`), your Linux
distribution, or the Qt online installer. SDL3 is fetched automatically.
When CMake can't find Qt, it builds everything except the GUI. Point it at
a Qt installation with `-DCMAKE_PREFIX_PATH=<Qt dir>`, or pass
`-DCUTENES_BUILD_GUI=ON` to make a missing Qt an error. The GUI is at
`build/app/cutenes` (`build/app/cutenes.app` on macOS). It keeps your
games in a library folder, `Documents/cuteNES` by default: put ROMs in its
`roms/` folder or add your own ROM folders (Add Folder… in the library),
and battery saves go to `saves/`. Change the location in
Settings > Library, or use `--library DIR` for one run. On Linux, install
the ALSA, PulseAudio and udev development headers first
(`libasound2-dev libpulse-dev libudev-dev` on Debian and Ubuntu), so SDL3
builds with real audio and gamepad support. Clone with `--recurse-submodules` to get the test-ROM archive
(`tests/roms`); GoogleTest is fetched at configure time.

## Using the core

```cmake
include(FetchContent)
FetchContent_Declare(cutenes-core
    GIT_REPOSITORY https://github.com/tdjustus/cuteNES.git
    GIT_TAG core-v0.1.0)
FetchContent_MakeAvailable(cutenes-core)
target_link_libraries(your_app PRIVATE cutenes-core::cutenes-core)
```

The public API contract is
[`specs/001-emulation-core/contracts/core-api.md`](specs/001-emulation-core/contracts/core-api.md).
For debugging, `Machine::dumpMemory("debug")` writes a `<datetime>.dmp` with
the CPU bus, PPU registers, OAM, palette, PRG-RAM and the whole PPU bus
(pattern tables, nametables, palette). `Machine::memoryDump()` returns the
same bytes without touching the filesystem. The layout is in the core
contract.

## Using the server

`cutenes-server` (built at `build/server/cutenes-server`) puts the core
behind MCP and a JSON-RPC API. One process holds one emulator session, and
every client shares it. The headless catalog has 54 commands: `core.*` (the
whole core API), `session.*` (status, save slots, blobs, debug dumps) and
`api.*` (version and catalog).

### MCP for AI agents (stdio)

With no flags the server speaks MCP on stdin/stdout and opens no socket.
Register it as a stdio server. In Claude Code:

```bash
claude mcp add cutenes -- /path/to/cutenes-server --library /path/to/roms
```

Or in any client's standard config:

```json
{ "mcpServers": { "cutenes": {
    "command": "/path/to/cutenes-server",
    "args": ["--library", "/path/to/roms"] } } }
```

`core.load_rom` takes paths relative to `--library` and rejects paths that
leave it. Without `--library`, ROMs can only be uploaded as blobs.
`core.screen` returns a PNG image block. The server serves MCP revisions
2026-07-28 (`server/discover`) and 2025-11-25 (`initialize`). It has been
checked with Claude Code (see
[`quickstart.md`](specs/002-ai-mcp-server/quickstart.md)). The server exits
at the end of its input, and calls still running stop at a frame boundary.

### API for scripts (HTTP)

```bash
cutenes-server --library ~/roms --api-port 8765 --token-file ./api.token &
TOKEN=$(jq -r .token api.token)
rpc() { curl -s -H "Authorization: Bearer $TOKEN" localhost:8765/api/v1/rpc -d "$1"; }
rpc '{"jsonrpc":"2.0","id":1,"method":"core.load_rom","params":{"path":"game.nes"}}'
rpc '{"jsonrpc":"2.0","id":2,"method":"core.run_frames","params":{"frames":120,"script":[["Start"]]}}'
rpc '{"jsonrpc":"2.0","id":3,"method":"core.peek","params":{"addr":24576,"length":16}}'
rpc '{"jsonrpc":"2.0","id":4,"method":"api.catalog"}'   # every command with its JSON Schemas
```

The endpoint is JSON-RPC 2.0 at `POST /api/v1/rpc`, with batches allowed.
`GET /api/v1/events` is a Server-Sent Events stream of state, frame and
catalog changes. `--mcp-port N` also serves MCP over Streamable HTTP at
`/mcp`, with its own token file (`--mcp-token-file`). Flags, errors and
events are in
[`contracts/transports.md`](specs/002-ai-mcp-server/contracts/transports.md).
Every command is in
[`contracts/command-catalog.md`](specs/002-ai-mcp-server/contracts/command-catalog.md).

### Versioning

The command surface has a `MAJOR.MINOR` version, currently 0.1. `api.version`
and MCP's `serverInfo.version` report it.

- Adding a command or an optional field bumps MINOR.
- Removing or renaming anything, or changing a field's type or meaning,
  bumps MAJOR.
- The committed catalog `tests/api/catalog.golden.json` fails the build when
  a schema changes without a version bump.

Before 1.0, a MAJOR of 0 means anything may still change.

### Security defaults

- **No network listener unless asked for.** With no flags, MCP runs over
  stdio only.
- **Loopback only.** HTTP listeners bind to `127.0.0.1` unless `--bind ADDR`
  is given.
- **Per-launch bearer tokens.** Each listener has its own token, which every
  request must carry (otherwise HTTP 401). The token file is JSON
  (`{"url", "token"}`), readable only by its owner, and is removed on a clean
  exit.
  - Default location: `~/Library/Application Support/cutenes/` on macOS,
    `$XDG_STATE_HOME/cutenes/` (or `~/.local/state/cutenes/`) on Linux, and
    `%LOCALAPPDATA%\cutenes\` on Windows.
  - Tokens are never logged.
- **Host and Origin checks.** A non-loopback `Host` or a foreign `Origin` is
  rejected with 403, which blocks DNS rebinding.
- **Bounded transfers.**
  - Reads are capped at 64 KiB per call and paged with `offset`/`length`.
  - Larger data moves through blob handles.
  - ROM paths cannot leave the library.

The Qt GUI (in progress, #32) will run the same API and MCP endpoints behind
settings toggles (G3, #95), both off by default. `cutenes-server --attach` relays a stdio-only
client to it.

## Testing

Tests are labelled in CTest. Counts are from the current tree.

| Label | Tests | What it proves | Runs in CI |
|---|---|---|---|
| `unit` | 187 | Component behavior written from hardware docs: CPU, PPU, APU, bus, scheduler, iNES parsing, each mapper's banking/mirroring/IRQ | every PR |
| `tier1` | 50 | The gate ROMs (nestest, instr_test-v5, instr_timing, ppu_vbl_nmi, apu_test, apu_reset, cpu_interrupts_v2, mmc3_irq_tests, ...) plus 13 golden frame hashes | every PR |
| `tier2` | 28 | Broader ROM suites incl. 22 Holy Mapperel board tests | every PR (Release builds) |
| `behavior` | 18 | Determinism (run twice, byte-identical), save-state round trip, instruction-step equivalence | every PR |
| `playtest`, `corpus` | 113 | Scripted ROM runs with on-screen text or pinned, hand-viewed frame hashes | every PR, except entries needing `roms-local/` |
| `api` | 98 | The `cutenes-api` command layer and HTTP API (feature 002): every `core.*`/`session.*` command against the contracts, a test ROM's own verdict through the commands, save-slot branching, structured errors that change no state, call ordering across threads and clients, JSON-RPC 2.0 framing, the token, Host and Origin checks, the frozen catalog, a GUI host's `gui.*` commands reaching both catalogs and MCP clients' list-changed notifications, and real-time play (one frame per requested frame, commands between frames, human and automation input combined, a host frame runner) | every PR |
| `mcp` | 35 | The MCP adapter (feature 002) against MCP's own pinned `schema.json` for both revisions it serves (2026-07-28 and 2025-11-25): discovery over stdio with the real executable, a test ROM's verdict and a PNG image block through tool calls, `tools/list` equal to `api.catalog`, Streamable HTTP's header and status rules, progress and subscription streams, cancellation when a client goes away, and the `--attach` bridge relaying a stdio client to a running instance (exit 4 when there is none) | every PR |
| `gui` | 134 | The Qt frontend (feature 003), offscreen and with SDL's dummy audio: the window and its seven menus in the spec's order, every contract item in the menus and the catalog (later ones disabled, returning `NotImplemented`), the menu actions' `gui.*` commands, shortcuts and platform roles, About's versions and license texts (Qt's LGPL included), SDL starting audio and gamepads, SIGTERM still ending the process, picture sizing (aspect, overscan, integer scaling), keyboard defaults, the frame hand-off, the frame clock, audio rate control within ±0.5%, the command line, playing a test ROM to its own "passed" in real time with the view showing the core's exact frame, game identity and file naming, the library layout, index and browser, recent games, Settings > Library, rewind (a rewound game in exactly the state a fresh run reaches, the history kept within budget, 30 s in far less than 64 MiB), fast-forward, speed and Frame Advance, a top-right indicator for rewind, fast rewind and speed in the style of PAUSED at half its size, save-state slots (the same hash after a load, a load waiting paused and then playing on with sound, a name asked for and a warning before replacing, states kept across a ROM rename, "PAUSED" over a paused game, messages that never resize the view, thumbnails, the selected slot per game, one level of undo, state files, another board's state refused), screenshots (the core's exact frame), Show FPS, on-screen messages that can be turned off, battery save import and export, Settings > Servers (off by default, MCP turning the API on, the token files `--attach` reads, a client turning the API off through itself, the automation indicator and an automation pause), and battery saves (written within the interval and on close, atomic, found by identity after a rename, a bad save set aside, one writer per game) | every PR (not the sanitizer job) |
| `known-failing` | 1 | A documented hardware discrepancy (#42) | never (run with `ctest -L known-failing`) |

What the numbers do not say: a golden hash records what this emulator draws,
not that it is right. Each of the 13 goldens carries a status (verified,
checked by eye, or unverified) in `tests/golden/roms.txt`. Commercial-game
playtests need ROMs you supply in the gitignored `roms-local/` folder and
skip otherwise. Open core issues are listed in the milestone. The corpus, how each folder is used and
the pinned-hash procedure are in [`docs/test-corpus.md`](docs/test-corpus.md).

CI (`.github/workflows/build.yml`) runs on pull requests, `core-v*` tags and
weekly: Release on Linux (GCC), macOS (AppleClang) and Windows (MSVC), an
MSVC Debug build with checked iterators, and an ASan/UBSan build with
libstdc++ assertions (the fast groups on pull requests, everything on tags
and weekly), plus a speed gate on the Linux job. A development machine runs the
core at roughly 7x real time; CI fails below 1x. Docs-only changes skip the
build.

### Harness

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

## Legal use

cuteNES is for playing games you own and for homebrew. The project does not
condone piracy and is designed not to enable it:

- It ships **no commercial ROMs**, and never will. The test suite uses
  freely licensed test ROMs; tests on commercial games run only on a
  developer's own copies in the gitignored `roms-local/`.
- Play your own dumps of cartridges you own, or homebrew and public-domain
  games. Do not download or share ROMs you have no right to.
- Features that touch other people's content are built the same way: game
  art (#90) uses only sources that allow it, and the planned `.nesx` format
  (#102) can never contain a ROM. It names its target by hash, and you
  supply your own copy.

## Project

- Constitution: [`.specify/memory/constitution.md`](.specify/memory/constitution.md)
- Feature specs and plans: [`specs/`](specs/)
- Source control, CI and releases: [`docs/source-control.md`](docs/source-control.md)
- Test-ROM provenance: [`third_party/NOTICE.md`](third_party/NOTICE.md)
- Embeddable Qt UI design:
  [`specs/001-emulation-core/ui-embeddable-design.md`](specs/001-emulation-core/ui-embeddable-design.md)
- Fix history and timing analysis:
  [`specs/001-emulation-core/analysis/`](specs/001-emulation-core/analysis/)
- License: MIT (see `LICENSE`).

### Branching and merging

The full strategy is in [`docs/source-control.md`](docs/source-control.md).
In short:

- `main` is protected and always green. Every change lands through a pull
  request from a `<kind>/<slug>` branch (`feature/`, `fix/`, `docs/`, `test/`,
  `chore/`), merged with a merge commit once the `ci-ok` check passes and the
  branch is up to date. Merged branches are deleted automatically.
- One branch at a time: each is merged before the next one starts, and
  branches are never stacked.
- CI runs on pull requests, on tags and weekly, not on every push and not
  again after the merge.
- Known bugs and roadmapped features are tracked in the GitHub milestone
  **1.0**. Version 1.0 ships when that milestone is empty.
- After 1.0, work moves to a long-lived `development` branch (never deleted)
  and each release merges `development` into `main`.
