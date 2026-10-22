# Implementation Plan: NES Emulation Core

**Branch**: `001-emulation-core` | **Date**: 2026-10-17 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `/specs/001-emulation-core/spec.md`

## Summary

A standalone, headless, deterministic NES emulation core delivered as the
`cutenes-core` C++20 library: complete 6502/PPU/APU hardware emulation at
the accuracy level required by the community test-ROM suites, iNES/NES 2.0
cartridge loading with the five initial mappers (NROM, MMC1, UxROM, CNROM,
MMC3), authentic controller polling, mid-frame save states, and
byte-identical video/audio output consumed via sinks. The technical approach
is settled by [research.md](research.md): the Mesen/puNES-class hybrid
architecture (instruction-level CPU + dot-stepped PPU caught up at every CPU
bus access against a single master clock), an all-integer emulation and
mixing path for cross-platform byte determinism, and a tiered test-ROM
acceptance harness wired into CI as the merge gate.

## Technical Context

**Language/Version**: C++20 (`cxx_std_20`), fixed-width integer types in all
emulated/serialized state.

**Primary Dependencies**: none at runtime — the core depends only on the
C++20 standard library (constitution Principle II: no Qt, no GUI, no I/O).
Test-only: GoogleTest via pinned FetchContent, standalone builds only.

**Storage**: N/A — no filesystem access in-core. Save states and battery
SRAM are byte ranges handed to consumers ([contracts/save-state-format.md](contracts/save-state-format.md)).

**Testing**: GoogleTest unit tests + `rom_harness` integration executable
(blargg $6000 protocol, nestest golden-log diff, determinism/save-state
scenarios, benchmarks) driven by ctest; tier-1 test-ROM submodule.
See [quickstart.md](quickstart.md).

**Target Platform**: Windows (MSVC 2022+), macOS (AppleClang 15+, x86-64 +
arm64), Linux (GCC 12+); headless CI containers on all three.

**Project Type**: library (`cutenes-core`), exported for FetchContent and
find_package consumption by the future Qt frontend and MCP server.

**Performance Goals**: 60.0988 fps NTSC / 50.0070 fps PAL sustained with >=
2x headroom (>= 120 fps equivalent), single-threaded.

**Constraints**: byte-identical determinism across platforms/compilers
(integer-only emulation path, `-ffp-contract=off` // `/fp:strict`); no
display/audio/windowing dependencies; single-threaded core with no wall-clock
reads; save-state format unstable (reject-on-mismatch) until first stable
release.

**Scale/Scope**: mappers 0/1/2/3/4; tier-1 acceptance suite (research.md
D11); 256x240@32bpp frames, 16-bit mono audio at 44.1/48/96 kHz.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Principle | Requirement | How this plan satisfies it | Verdict |
|---|---|---|---|
| I. Hardware Fidelity | nesdev-normative behavior, cited decisions | research.md registers every timing constant with its nesdev source; flagged disagreements (D2) documented rather than guessed | PASS |
| II. Core/UI Separation | standalone headless library, no Qt/GUI/MCP types | `cutenes-core` has zero runtime deps; consumers attach via exported CMake target | PASS |
| III. Test ROMs Are the Gate | automated suite passes before merge | Tier-1 suite + nestest golden log run in ctest on all 3 OS in CI; merge gate defined in quickstart.md | PASS |
| IV. Cross-Platform by Construction | Win/macOS/Linux single codebase, Qt-sanctioned only (core has none) | CMake 3.21, default toolchains, 3-OS matrix incl. arm64; no platform-specific code anywhere in core | PASS |
| V. Automation Parity | every capability exposed for programmatic use | [contracts/core-api.md](contracts/core-api.md) ends with the capability table feature 002 audits against | PASS |

**Emulation & Platform Constraints check**: MIT for project code (this
feature adds no third-party runtime code; GoogleTest is BSD-3, test-only,
never shipped); test-ROM submodule strategy keeps unlicensed binaries out of
the MIT tree with `third_party/NOTICE.md` provenance (research.md D11);
performance targets adopted as SC-003.

**Post-Phase-1 re-check**: data-model, contracts, and quickstart introduce
no Qt dependency, no vague accuracy language, and keep every FR traceable to
an API row or harness scenario. No new violations. **PASS**.

## Project Structure

### Documentation (this feature)

```text
specs/001-emulation-core/
├── plan.md                     # This file
├── research.md                 # Phase 0 output — 12 cited decisions
├── data-model.md               # Phase 1 output — entities + state inventory
├── quickstart.md               # Phase 1 output — validation scenarios
├── contracts/
│   ├── core-api.md             # Public C++ API + capability table
│   ├── media-output.md         # Frame/audio formats, hashing conventions
│   └── save-state-format.md    # Blob layout, sections, rejection rules
└── tasks.md                    # Phase 2 (/speckit-tasks — not created yet)
```

### Source Code (repository root)

```text
CMakeLists.txt                  # cmake_minimum_required(3.21); project(cutenes);
                                # add_subdirectory(core); guarded tests/apps
core/
├── CMakeLists.txt              # cutenes-core + alias cutenes-core::cutenes-core,
│                               # cxx_std_20 PUBLIC, strict-FP + warning flags (PRIVATE),
│                               # install(TARGETS ... EXPORT) + package config
├── include/cutenes/            # public headers == contracts/core-api.md
│   ├── cutenes.hpp             # umbrella include
│   ├── machine.hpp             # Machine, MachineConfig, run control
│   ├── cartridge.hpp           # LoadResult, LoadError, CartridgeInfo, Mirroring
│   ├── controller.hpp          # Port, Button, ButtonSet
│   ├── state_view.hpp          # CpuState, memory/register access
│   ├── media.hpp               # VideoFrame, IVideoSink, IAudioSink
│   └── save_state.hpp          # SaveStateBlob, LoadStateError, SRAM API
└── src/cutenes/
    ├── system/                 # machine.cpp, scheduler (master clock, catch-up),
    │                           # region profiles (NTSC/PAL constants), reset sequence
    ├── cpu/                    # 6502 decoder, addressing, cycle-exact bus access wrappers
    ├── ppu/                    # dot stepper, rendering pipeline, sprite evaluation SM
    ├── apu/                    # channels, frame counter, DMC DMA arbiter,
    │                           # integer LUT mixer, fixed-point band-limited downsampler
    ├── cartridge/              # iNES 1.0 / NES 2.0 parser, SRAM sizing
    ├── mappers/                # nrom, mmc1, uxrom, cnrom, mmc3 (+ registry)
    ├── bus/                    # memory map, register file, OAM DMA engine
    ├── input/                  # serial controller polling
    └── serialize/              # save-state codec (header, sections, CRC-32)
tests/
├── CMakeLists.txt              # GoogleTest FetchContent (standalone only),
│                               # gtest_discover_tests
├── unit/                       # per-component unit tests
├── harness/                    # rom_harness CLI: suite|nestest|determinism|
│                               # savestate|playtest|bench (quickstart scenarios)
└── roms/                       # git submodule -> christopherpow/nes-test-roms
third_party/
├── holy-mapperel/              # vendored zlib-licensed mapper test ROMs + LICENSE
└── NOTICE.md                   # provenance for every redistributed test artifact
.github/workflows/build.yml     # 3-OS matrix (Release+Debug) + Linux UBSan job
```

**Structure Decision**: single-root CMake with a physically separate `core/`
subdirectory (constitution Principle II made visible in the tree), consumer
apps arriving later under `apps/` — the root guard already reserves that
slot. Tests live outside `core/` so the library target never sees test
dependencies; the ROM submodule lives under `tests/roms/` and is checked out
in CI but never packaged. Public headers mirror [contracts/core-api.md](contracts/core-api.md)
one-to-one, which is how the contract stays enforceable in review.

## Complexity Tracking

No constitution violations to justify — no violations were identified in
either constitution check. Table intentionally empty.

## UI Architecture (Future)

An embeddable Qt 6 emulation view is planned for future implementation. See [ui-embeddable-design.md](ui-embeddable-design.md) for:
- Embeddable `EmulationView` widget design with strict core/UI separation
- Hardware-accelerated rendering (OpenGL) with raster fallback
- QtMultimedia audio integration via `IAudioSink`
- Flexible input mapping for keyboard/gamepad
- Library-first architecture (`cutenes-qt`) with find_package support
- Multiple instance support and DPI awareness

The UI layer will consume only the public core API, maintaining compliance with Constitution Principle II.

### Debugging Support
- Add `Machine::dumpMemory()` to write timestamped `.dmp` files to a debug directory with memory snapshots for analysis.
