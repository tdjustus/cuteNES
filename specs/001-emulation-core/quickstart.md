# Quickstart: NES Emulation Core

**Feature**: 001-emulation-core | **Purpose**: runnable validation that the
core works end-to-end. Implementation detail belongs to tasks.md, not here.

## Prerequisites

- CMake >= 3.21 and a C++20 compiler (MSVC 2022+, AppleClang 15+, or GCC 12+)
- Git (test-ROM submodule fetch: `git clone --recurse-submodules`)
- No display, audio device, or GUI toolkit is needed anywhere below

## Setup and build

```bash
git clone --recurse-submodules <repo> cutenes && cd cutenes
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

Expected: `cutenes-core` library target builds with zero dependencies beyond
the C++20 standard library; the `rom_harness` and unit test executables build
from `tests/` (GoogleTest is fetched automatically in standalone builds only).

## Validation scenarios

### 1. Unit and integration suite

```bash
ctest --test-dir build --output-on-failure
```

**Expected**: all component unit tests (CPU decoder, PPU rendering cases,
APU channel behavior, iNES/NES 2.0 parser, mapper banking, save-state codec)
and tier-1 ROM integration tests pass on all three platforms (SC-001, SC-005).

### 2. CPU golden-log verification (nestest)

```bash
./build/tests/rom_harness nestest --golden-log
```

**Expected**: the harness boots nestest in automation mode (PC=$C000), diffs
a per-instruction CPU trace against the vendored `nestest.log`, reports the
first divergent line if any, and reads result bytes $02/$03 == 0.
**Passes when**: the full log matches through the final line and both result
bytes are zero (US2 scenario 1).

### 3. Headless test-ROM acceptance (tier 1)

```bash
./build/tests/rom_harness suite --tier 1
```

**Expected**: each ROM is driven until its $6000 status leaves $80 (honoring
the $81 reset handshake with >=100 ms emulated delay); every ROM reports
result code 0 with its $6004 text captured in the log. Covers instr_test-v5,
instr_timing, cpu_interrupts_v2, cpu_reset, ppu_vbl_nmi, oam_read,
oam_stress, ppu_open_bus, sprdma_and_dmc_dma, apu_test, apu_reset,
mmc3_test(_2) — per the tier list in research.md D11 (SC-001).

### 4. Deterministic replay

```bash
./build/tests/rom_harness determinism --rom <game.nes> --frames 2000
```

**Expected**: two runs from identical power-on state produce identical
per-frame video hashes and byte-identical audio; report shows zero diffs
(FR-012, SC-002).

### 5. Save/restore round trip

```bash
./build/tests/rom_harness savestate --rom <game.nes> --at-frame 1000 --rerun-to 2000
```

**Expected**: run to frame 1000, capture, continue to 2000 recording hashes;
restore the frame-1000 blob, rerun — hashes match the original run exactly
(FR-011, SC-006). Also verifies rejection: a blob from a different core
build is refused with `IncompatibleCoreVersion`.

### 6. Commercial-game smoke test (mapper coverage)

```bash
./build/tests/rom_harness playtest --list tests/roms/playtest.txt
```

**Expected**: each entry (curated list spanning mappers 0/1/2/3/4) boots,
reaches gameplay per its scripted input, and reports per-frame hash
stability class; unsupported-mapper ROMs exit with a named
`UnsupportedMapper` error, never silent misbehavior (US6 scenarios).

### 7. Performance headroom

```bash
./build/tests/rom_harness bench --rom <game.nes> --seconds 10
```

**Expected**: sustained emulation rate >= 120 fps NTSC (2x real-time, SC-003)
on a commodity 2020s machine, single-threaded.

## CI

`.github/workflows/build.yml` runs scenarios 1-5 on
ubuntu/macos/windows-latest (Release + Debug) plus a Linux UBSan job.
Tier-1 suite green on all three platforms is the merge gate
(constitution Principle III).
