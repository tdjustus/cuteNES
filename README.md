# cuteNES

An open-source, cross-platform NES emulator built on a hardware-accurate,
headless, deterministic emulation core — with a first-class MCP server so
AI agents can connect to and drive the emulator.

- **Core**: native C++20, no runtime dependencies, byte-identical
  cross-platform output, validated against the community test-ROM suites.
- **Frontends** (planned): Qt 6 desktop app; MCP server for AI automation.
- **Normative hardware reference**: the [NESdev NES Reference Guide]
  (https://www.nesdev.org/wiki/NES_reference_guide).

## Building the core

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Requires CMake >= 3.21 and a C++20 compiler (MSVC 2022+, AppleClang 15+,
or GCC 12+). Tests fetch GoogleTest at configure time (standalone builds
only) and need `git clone --recurse-submodules` for the test-ROM archive.

## Consuming the core

```cmake
# From the Qt app / MCP server:
FetchContent_Declare(cutenes-core GIT_REPOSITORY ... GIT_TAG v0.1.0)
FetchContent_MakeAvailable(cutenes-core)
target_link_libraries(your_app PRIVATE cutenes-core::cutenes-core)
```

Public API contract: `specs/001-emulation-core/contracts/core-api.md`.

## MMC1 revision (mapper 1 vs 155)

The MMC1 ASIC exists in behaviorally distinct revisions, and the difference
is in the PRG-bank register's bit 4 (nesdev wiki, MMC1 — "ASIC
Revisions"):

- **MMC1B** ("normal", iNES mapper 1): bit 4 of `$E000` disables PRG-RAM —
  reads float and writes are dropped while set. This is the behavior of
  the vast majority of cartridges.
- **MMC1A** (iNES mapper 155): PRG-RAM is always enabled. Bit 4 instead
  makes register bit 3 drive PRG A17 directly across the entire
  `$8000-$FFFF` range, bypassing the fixed-bank logic — visible only with
  256 KiB PRG. Two known games rely on this (they write `$10-$1F` to the
  PRG-bank register and expect WRAM to stay writable).

Both revisions honor the consecutive-cycle write rule: a serial-port write
on the cycle immediately after another contributes no data bit (the 6502
read-modify-write double write), while the bit-7 reset is never ignored
(*Bill & Ted's Excellent Adventure* and *Shinsenden* depend on this).

Select the revision through the header: mapper 155 means MMC1A; mapper 1
(or any plain iNES 1.0 header) means MMC1B.

## MMC3 revision (mapper 4 IRQ behavior)

The MMC3 exists in two behaviorally distinct silicon revisions, and the
difference shows only in the scanline IRQ counter (nesdev wiki, MMC3 —
"IRQ Specifics"):

- **Revision B ("normal")** asserts the IRQ whenever the counter is 0, so a
  reload value of `$00` IRQs every scanline. This is the behavior of the
  boards in Super Mario Bros. 3 and Mega Man 3, and of the vast majority of
  cartridges.
- **Revision A ("alternate")** asserts only when the counter *changes* to 0 —
  by decrementing, or by a `$C001` reload landing on 0 (the write first puts
  the counter above 127). Found in Crystalis-era boards.

The two are mutually exclusive at the hardware level, and the emulator picks
between them as follows:

1. **Header (preferred):** an NES 2.0 header selects the revision through the
   submapper field — mapper 4, submapper 4 means revision A; submapper 0 (or
   any plain iNES 1.0 header, which cannot express the choice) means revision
   B. If you have a revision A dump (e.g. Crystalis), tag its header NES 2.0
   with submapper 4 to get the exact IRQ behavior.
2. **Manual override (any time):** `machine.setMmc3Revision(
   Machine::Mmc3Revision::A)` forces a revision regardless of the header —
   also `::B` to force the other way, and `::Auto` (the default) to follow
   the header. The setting applies immediately to a loaded cartridge and to
   subsequent loads, and it is part of the save state.

All six blargg `mmc3_irq_tests` ROMs pass: 1-4 and 6 under revision B,
`5.MMC3_rev_A` under an NES 2.0 submapper 4 header (the playtest suite does
exactly this via its `force-submapper` directive).

Caveat: for nearly all games the two revisions are indistinguishable, since
the behavior only diverges when a game writes a reload value of `$00` or
otherwise depends on the change-to-zero nuance. The header tagging (or manual
override) therefore matters only for that small set of titles — Crystalis is
the known revision A case. An untagged revision A dump still runs; it simply
gets the revision B rule, which is byte-identical in practice for anything
that never exercises the divergent cases.

## Project governance

- Constitution: `.specify/memory/constitution.md`
- Feature specs & plans: `specs/`
- Licensing: MIT for project code (see LICENSE); test-ROM provenance in
  `third_party/NOTICE.md`.

### Branching & merging

`main` is always green: every change lands through a branch and a pull
request, merged only after the CI matrix (ubuntu/macos/windows x
Release/Debug plus UBSan) passes.

- Branch from `main` as `<kind>/<slug>`: `feat/`, `fix/`, `docs/`,
  `test/`, `chore/` (matching the commit-prefix convention).
- One logical change per branch; keep commits prefixed the same way
  (`feat(api): ...`).
- Open a PR against `main`; CI runs the full gate including the
  cross-platform `golden-hashes` audit. Do not self-merge while the
  matrix is red.
- Merge with a merge commit (no squash) to preserve per-change commit
  messages, then delete the branch.
- Tags (`core-v0.1.0`, ...) are cut from `main` only after its CI run is
  green.
- Never push directly to `main`; hotfixes follow the same path on
  `fix/`.

## Status

Feature 001 (emulation core): see `specs/001-emulation-core/tasks.md`.
The tier-1 gate runs the standalone sub-test ROMs for ppu_vbl_nmi and
apu_test, plus nestest, instr_test-v5, instr_timing, cpu_reset,
oam_read, and apu_reset, and the six blargg mmc3_irq_tests ROMs. All
formerly known-failing tests are fixed (T068/T069 closed; the MMC3
revision A/B split is header-selectable, see above). Save states
round-trip byte-identically; 600-frame determinism and
instruction-stepping equivalence verified; ~592 fps single-threaded
(9.9x real-time).

## Harness

```bash
build/tests/rom_harness nestest                 # CPU golden log
build/tests/rom_harness suite --rom <rom>       # blargg $6000 protocol
build/tests/rom_harness determinism --rom R --frames N
build/tests/rom_harness savestate --rom R --at F --rerun-to G
build/tests/rom_harness step-equiv R N          # stepping == runFrames
build/tests/rom_harness bench --rom R           # sustained speed

- **Embeddable UI Design**: Qt 6 widget architecture designed for reuse in other applications ([specs/001-emulation-core/ui-embeddable-design.md](specs/001-emulation-core/ui-embeddable-design.md)). Strict core/UI separation via public API, OpenGL-accelerated rendering, and library-first CMake integration.

### Debugging
- `Machine::dumpMemory("debug")` creates a `<datetime>.dmp` file in the debug directory with snapshots of CPU memory, PPU memory, OAM, palette, and cartridge RAM/ROM for troubleshooting.
