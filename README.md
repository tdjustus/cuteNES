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

## Project governance

- Constitution: `.specify/memory/constitution.md`
- Feature specs & plans: `specs/`
- Licensing: MIT for project code (see LICENSE); test-ROM provenance in
  `third_party/NOTICE.md`.

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
