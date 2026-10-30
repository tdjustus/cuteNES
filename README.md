# cuteNES

[![build](https://github.com/tdjustus/cuteNES/actions/workflows/build.yml/badge.svg)](https://github.com/tdjustus/cuteNES/actions/workflows/build.yml)

An open-source, cross-platform NES emulator built on a hardware-accurate,
headless, deterministic emulation core, with a planned MCP server so AI agents
can connect to and drive the emulator.

- **Core** (`core/`): native C++20, no runtime dependencies, deterministic
  (the same ROM and inputs give byte-identical video and audio on every
  platform), validated against community test-ROM suites.
- **Frontends** (planned, not started): a Qt 6 desktop app and an MCP server
  ([`specs/002-ai-mcp-server`](specs/002-ai-mcp-server)).
- **Normative hardware reference**: the
  [NESdev NES Reference Guide](https://www.nesdev.org/wiki/NES_reference_guide).

## Status

The emulation core ([spec 001](specs/001-emulation-core/spec.md)) is
feature-complete for the mappers below and is hardened toward version 1.0.
What is left is tracked in the GitHub
[1.0 milestone](https://github.com/tdjustus/cuteNES/milestone/1): a small
number of timing edge cases, the MCP server and the Qt frontend. See
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
GCC 12+). Clone with `--recurse-submodules` to get the test-ROM archive
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

## Testing

Tests are labelled in CTest. Counts are from the current tree.

| Label | Tests | What it proves | Runs in CI |
|---|---|---|---|
| `unit` | 185 | Component behavior written from hardware docs: CPU, PPU, APU, bus, scheduler, iNES parsing, each mapper's banking/mirroring/IRQ | every PR |
| `tier1` | 50 | The gate ROMs (nestest, instr_test-v5, instr_timing, ppu_vbl_nmi, apu_test, apu_reset, cpu_interrupts_v2, mmc3_irq_tests, ...) plus 13 golden frame hashes | every PR |
| `tier2` | 28 | Broader ROM suites incl. 22 Holy Mapperel board tests | every PR (Release builds) |
| `behavior` | 18 | Determinism (run twice, byte-identical), save-state round trip, instruction-step equivalence | every PR |
| `playtest`, `corpus` | 113 | Scripted ROM runs with on-screen text or pinned, hand-viewed frame hashes | every PR, except entries needing `roms-local/` |
| `api` | 58 | The `cutenes-api` command layer (feature 002): every `core.*`/`session.*` command against the contracts, a test ROM's own verdict through the commands, save-slot branching, structured errors that change no state, call ordering across threads, and the frozen catalog | every PR |
| `known-failing` | 1 | A documented hardware discrepancy (#42) | never (run with `ctest -L known-failing`) |

What the numbers do not say: a golden hash records what this emulator draws,
not that it is right. Each of the 13 goldens carries a status (verified,
checked by eye, or unverified) in `tests/golden/roms.txt`. Commercial-game
playtests need ROMs you supply in the gitignored `roms-local/` folder and
skip otherwise. Open core issues are listed in the milestone. The corpus, how each folder is used and
the pinned-hash procedure are in [`docs/test-corpus.md`](docs/test-corpus.md).

CI (`.github/workflows/build.yml`) runs on pull requests, `core-v*` tags and
weekly: Release on Linux, macOS and Windows, a Windows Debug build, and an
ASan/UBSan build (the fast groups on pull requests, everything on tags and
weekly), plus a speed gate on the Linux job. A development machine runs the
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
