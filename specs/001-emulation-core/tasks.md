---
description: "Task list for NES emulation core implementation"
---

# Tasks: NES Emulation Core

**Input**: Design documents from `/specs/001-emulation-core/`

**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/, quickstart.md — all present.

**Tests**: INCLUDED — constitution Principle III mandates test-first development
for core behavior (tests → observed failure → implementation). Test tasks are
listed first in each story phase.

**Organization**: Tasks grouped by user story (spec.md P1→P6), each story an
independently testable increment. Paths follow plan.md's source tree.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: parallelizable (different files, no dependencies on incomplete tasks)
- **[Story]**: user story (US1..US6)
- Exact file paths in every task

## Path Conventions

- Library: `core/include/cutenes/` (public API), `core/src/cutenes/` (implementation)
- Tests: `tests/unit/`, `tests/harness/` (rom_harness), `tests/roms/` (submodule)
- Build: root `CMakeLists.txt` per plan.md Structure Decision

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Build system, public API skeleton, CI, licensing, test-ROM access.

- [x] T001 Create root `CMakeLists.txt`: `cmake_minimum_required(3.21)`, `project(cutenes VERSION 0.1.0 LANGUAGES CXX)`, `add_subdirectory(core)`, guarded `tests/` behind `CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR`
- [x] T002 Create `core/CMakeLists.txt`: target `cutenes-core` + alias `cutenes-core::cutenes-core`, `target_compile_features(cutenes-core PUBLIC cxx_std_20)`, PRIVATE flags `-ffp-contract=off` (GCC/Clang) / `/fp:strict` (MSVC), `-Wconversion -Werror`, `install(TARGETS ... EXPORT)` + `install(EXPORT)` + versioned package config (plan.md, research D6)
- [x] T003 [P] Create public header skeleton mirroring contracts/core-api.md: `core/include/cutenes/{cutenes.hpp, machine.hpp, cartridge.hpp, controller.hpp, state_view.hpp, media.hpp, save_state.hpp}` — types and signatures only, no implementation
- [x] T004 [P] Create `tests/CMakeLists.txt`: GoogleTest via pinned FetchContent (`gtest_force_shared_crt ON`), `enable_testing()`, `gtest_discover_tests()`, ctest label scheme `unit|tier1|tier2`
- [x] T005 [P] Add `LICENSE` (MIT, copyright Timothy Justus), `third_party/NOTICE.md` (per-item provenance template), `README.md` stub linking constitution and specs
- [x] T006 [P] Add test-ROM submodule `tests/roms` -> `christopherpow/nes-test-roms` (pinned commit) via `.gitmodules`; vendor zlib-licensed Holy Mapperel ROMs + LICENSE copy under `third_party/holy-mapperel/` (research D11)
- [x] T007 [P] Create `.github/workflows/build.yml`: 3-OS matrix (ubuntu/macos/windows-latest, Release+Debug, Ninja, ccache/sccache via `CMAKE_CXX_COMPILER_LAUNCHER`), submodule checkout, `ctest --output-on-failure`, dedicated Linux UBSan job `-fsanitize=undefined -fno-sanitize-recover=all` (research D12)

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Machine skeleton every story builds on — clock, bus, reset, sinks, harness frame.

- [x] T008 Implement region profiles + master-clock scheduler in `core/src/cutenes/system/region.cpp` and `core/src/cutenes/system/scheduler.cpp`: single `u64` master counter, NTSC "12 master ticks per CPU cycle (6 per half-cycle), 4 per PPU dot", PAL 16/5, master clock 236,250,000/11; constants table from research.md D2 (341 dots/scanline, 262/312 scanlines, 29,780.5 CPU cycles/frame NTSC, 60.0988/50.0070 fps)
- [x] T009 Implement bus skeleton in `core/src/cutenes/bus/bus.cpp`: 2KB work RAM + mirroring, address-space regions ($0000-$1FFF RAM, $2000-$3FFF PPU mirrors, $4000-$401F APU/IO, $4020-$FFFF cartridge), open-bus placeholder, PRG-RAM $6000-$7FFF writable (blargg protocol requirement, research D11)
- [x] T010 Implement power-on/reset in `core/src/cutenes/system/machine.cpp`: A=X=Y=0, S=$FD, "P = $24 (bit 5 always reads 1; B flag ... never materialized)", PC from $FFFC/$FFFD; "reset sequence consumes 7 CPU cycles" so CYC:7/PPU:(0,21) matches nestest.log line 1; PPU writes ignored ~29,658 cycles after reset (research D4)
- [x] T011 Implement media sink plumbing in `core/src/cutenes/system/machine.cpp` + `core/include/cutenes/media.hpp`: `IVideoSink::onFrame` (256x240 XRGB8888 little-endian, "61440 bytes/frame", default palette table as contract-frozen data) and `IAudioSink::onSamples` (mono int16, bounded <= 4096-sample chunks), synchronous, never blocking (contracts/media-output.md)
- [x] T012 Implement rom_harness skeleton in `tests/harness/main.cpp`: subcommands `suite|nestest|determinism|savestate|playtest|bench` (arg parsing + dispatch stubs), run-frames runner, FNV-1a 32 video-hash + audio-hash over delivery order (contracts/media-output.md hashing conventions)
- [x] T013 [P] Write unit tests `tests/unit/test_scheduler.cpp` (divider math, frame boundaries NTSC/PAL) and `tests/unit/test_bus.cpp` (mirror decode, RAM read/write) — verify RED before T008/T009 land, then green

**Checkpoint**: `cmake --build` + `ctest -L unit` green on all 3 platforms; Machine powers on and delivers empty frames.

---

## Phase 3: User Story 1 - Headless Deterministic Game Execution (Priority: P1) 🎯 MVP

**Goal**: Load an NROM commercial ROM, execute frame by frame headlessly, deliver byte-identical video/audio as data (spec US1).

**Independent Test**: `rom_harness determinism --rom <nrom-game> --frames 60` — two runs produce identical per-frame video hashes and byte-identical audio, in a display-less CI container (quickstart scenarios 1, 4).

### Tests for User Story 1 (write first, verify RED)

- [x] T014 [P] [US1] Write `tests/unit/test_ines.cpp`: valid iNES/NES 2.0 headers, each `LoadError` case (NotAnInesFile, TruncatedFile, InvalidHeader, InvalidBankCounts, Nes2FieldInvalid, UnsupportedMapper naming the id), trainer parsing, flags (mirroring, battery, PRG-RAM)
- [x] T015 [P] [US1] Write `tests/unit/test_cpu.cpp`: opcode decode + cycle-count table for documented and unofficial opcodes (data-driven over the full 256-entry space), page-cross penalties, dummy-read expectations
- [x] T016 [P] [US1] Write `tests/unit/test_ppu.cpp`: nametable mirroring decode, fine/coarse scroll math, sprite attribute decode, palette RAM behavior
- [x] T017 [P] [US1] Write `tests/unit/test_apu.cpp`: per-channel timer/frequency math, length counters, envelope steps, frame-counter step sequence NTSC/PAL, mixer LUT spot values vs the nesdev formulas (test oracle = exact formula, research D8)

### Implementation for User Story 1

- [x] T018 [US1] Implement iNES 1.0 + NES 2.0 parser in `core/src/cutenes/cartridge/ines.cpp` filling `CartridgeInfo` exactly per contracts/core-api.md (LoadError + detail code on every rejection path)
- [x] T019 [US1] Implement 6502 documented instruction set in `core/src/cutenes/cpu/decoder.cpp` + `core/src/cutenes/cpu/cpu.cpp`: all addressing modes, exact cycle counts, dummy reads on RMW/indexed reads, page-cross penalties; every bus access bracketed by half-cycle clock advance + PPU catch-up (research D1)
- [x] T020 [US1] Implement unofficial opcodes (LAX, SAX, DCP, ISB, SLO, RLA, SRE, RRA, multi-byte NOPs) in `core/src/cutenes/cpu/decoder.cpp` with documented cycle counts
- [x] T021 [US1] Implement PPU background pipeline in `core/src/cutenes/ppu/ppu.cpp` + `core/src/cutenes/ppu/rendering.cpp`: dot/scanline counters, fetch cycles (nametable/attribute/pattern low/high), shift registers, fine-X, scrolling, mirroring, rendering-disabled behavior, emphasis/grayscale bits
- [x] T022 [US1] Implement sprite rendering in `core/src/cutenes/ppu/sprite_eval.cpp`: 8-per-scanline limit, priority, horizontal/vertical flip, 8x16 mode, left-clip masks (dot-exact evaluation bug handling deferred to US2 T035)
- [x] T023 [US1] Implement VBlank flag + NMI in `core/src/cutenes/ppu/ppu.cpp`: set (241,1), clear (261,1) + on $2002 read, NMI enable edge, basic frame-IRQ plumbing
- [x] T024 [US1] Implement APU in `core/src/cutenes/apu/{apu.cpp, channels.cpp, frame_counter.cpp, mixer.cpp, downsampler.cpp}`: pulse x2/triangle/noise/DMC, integer 31-entry pulse + 203-entry TND LUTs (double math at init only), int32 mix + clamp, fixed-point one-pole HPF 90 Hz / HPF 440 Hz / LPF 14 kHz, fixed-point band-limited downsampler to configured rate (44100/48000/96000), mono int16 output (research D8)
- [x] T025 [US1] Implement mapper registry + NROM in `core/src/cutenes/mappers/{mapper.cpp, nrom.cpp, registry.cpp}`: interface for CPU/PPU bus hooks (research D1 component design)
- [x] T026 [US1] Implement frame delivery in `core/src/cutenes/system/machine.cpp`: palette-index-to-XRGB8888 with the frozen default palette, frame emitted when "the final visible pixel (scanline 239, dot 255) is emitted", exactly once per frame
- [x] T027 [US1] Implement determinism scenario in `tests/harness/determinism.cpp`: run-twice hash comparison over N frames + audio bytes; zero-diff report (SC-002)
- [x] T028 [US1] Integration checkpoint: run a commercial NROM title 60 frames headless in CI (no display/audio) and record golden hashes as the regression baseline (US1 scenarios 1-2)

**Checkpoint**: US1 complete — games run headlessly, deterministically. This is the MVP.

---

## Phase 4: User Story 2 - Accuracy Verified Against Test ROMs (Priority: P2)

**Goal**: Tier-1 suite green headlessly in CI on all 3 platforms (spec US2, FR-015, SC-001).

**Independent Test**: `ctest -L tier1` passes on ubuntu/macos/windows (quickstart scenarios 2, 3).

### Implementation for User Story 2

- [x] T029 [US2] Implement CPU trace/disassembly in `core/src/cutenes/cpu/trace.cpp` matching nestest.log line format exactly (`PC, A, X, Y, P, SP, opcode bytes + disassembly, CYC, SL`); wire `rom_harness nestest --golden-log` in `tests/harness/nestest.cpp` (automation mode PC=$C000, results at $02 official / $03 unofficial, first-divergent-line report)
- [x] T030 [US2] Implement blargg console protocol driver in `tests/harness/protocol.cpp`: signature `$DE $B0 $61` at $6001-$6003, status polling at $6000 ($80 running, $81 reset-request honored with >=100 ms emulated delay, $00-$7F result), $6004 zero-terminated text capture (research D11)
- [x] T031 [US2] Implement tier-1 suite runner in `tests/harness/suite.cpp`: drive each tier-1 ROM to a terminal status, register as ctest cases with `tier1` label; list per research D11 (instr_test-v5, instr_timing, cpu_interrupts_v2, cpu_reset, ppu_vbl_nmi, oam_read, oam_stress, ppu_open_bus, sprdma_and_dmc_dma, apu_test, apu_reset, mmc3_test, mmc3_test_2 — the last two execute in US6)
- [x] T032 [US2] Drive instr_test-v5 + instr_timing to pass: fix decoder defects surfaced by T029-T031 (dummy reads, unofficial-opcode edge cases) in `core/src/cutenes/cpu/decoder.cpp`
- [x] T033 [US2] Implement interrupt semantics in `core/src/cutenes/cpu/interrupts.cpp`: NMI/IRQ polling at final instruction cycle, CLI/SEI/PLP one-instruction delay, NMI hijacks BRK, NMI suppression when $2002 read at the set-dot — pass cpu_interrupts_v2 + ppu_vbl_nmi (research D2)
- [x] T034 [US2] Complete PPU dot precision in `core/src/cutenes/ppu/ppu.cpp`: exact VBlank set/clear dots, $2002 read races at one-PPU-clock accuracy, odd-frame dot skip when rendering enabled — pass ppu_vbl_nmi all subtests (US2 scenario 2)
- [x] T035 [US2] Implement the buggy sprite-overflow diagonal scan (n and m both increment on miss) + dot-exact sprite-0 hit with all exclusions (x=255, left clip, disabled layers, transparent pixels) in `core/src/cutenes/ppu/sprite_eval.cpp` — pass sprite_overflow_tests and sprite_hit_tests tier-2 checks (research D3)
- [x] T036 [US2] Drive apu_test + apu_reset to pass in `core/src/cutenes/apu/{frame_counter.cpp, dmc.cpp}`: frame-counter steps/IRQ/inhibit + reset register state ($4015 cleared, $4011 &= 1)
- [ ] T037 [US2] Implement DMA engines: OAM DMA 513/514 with get/put alignment in `core/src/cutenes/bus/oam_dma.cpp`; DMC DMA 3-4 cycle load/reload with get-priority over OAM DMA in `core/src/cutenes/apu/dmc_dma.cpp` — pass sprdma_and_dmc_dma (research D2/D8-A3)
- [x] T038 [US2] Make tier-1 green the CI merge gate: `.github/workflows/build.yml` fails the build on any `tier1` failure on any OS (constitution Principle III)

**Checkpoint**: Accuracy is proven, not assumed — every merge runs the hardware-validated suite.

---

## Phase 5: User Story 3 - Programmatic Run Control (Priority: P3)

**Goal**: Full run-control API with pause/resume/frame-advance/single-step/reset (spec US3, FR-008/FR-016).

**Independent Test**: `rom_harness` pause → single-step N instructions → resume produces the same frame hashes as the uninterrupted run (US3 independent test).

### Implementation for User Story 3

- [x] T039 [P] [US3] Implement run-control state machine in `core/src/cutenes/system/machine.cpp`: `runFrames(n)`, `frameAdvance()`, `pause()`, `resume()`, `RunState` transitions (NeedsRom→Paused→Running) per data-model.md Machine entity
- [x] T040 [US3] Implement `stepInstruction()` in `core/src/cutenes/system/machine.cpp` + `core/src/cutenes/system/scheduler.cpp`: advance dependent components to the exact next instruction boundary
- [x] T041 [P] [US3] Implement authentic `reset()` vs `powerOn()` differences in `core/src/cutenes/system/machine.cpp`: reset keeps A/X/Y/flags (sets I=1), S -= 3, PC re-fetch from $FFFC, "$4015 cleared, $4011 &= 1, $4017 unchanged" (research D4)
- [x] T042 [P] [US3] Implement observation API in `core/src/cutenes/system/machine.cpp` + `core/include/cutenes/state_view.hpp`: `cpu()`, `readMemory` (register-read side effects included), `writeMemory`, `writeCpuRegister` (contracts/core-api.md)
- [x] T043 [US3] Add stepping-equivalence scenario to `tests/harness/main.cpp`: interleaved pause/step/resume sessions hash-identical to uninterrupted runs

**Checkpoint**: Automation clients can drive execution precisely (foundation for MCP feature 002).

---

## Phase 6: User Story 4 - Controller Input Injection (Priority: P4)

**Goal**: Injected input indistinguishable from physical (spec US4, FR-007).

**Independent Test**: Scripted Start-button sequence navigates a game's title menu identically to physical play (US4 scenario 1).

### Implementation for User Story 4

- [ ] T044 [P] [US4] Write `tests/unit/test_controllers.cpp` (serial shift order Right,Left,Down,Up,Start,Select,B,A; strobe behavior; open-bus bits on $4016/$4017 reads), then implement in `core/src/cutenes/input/controllers.cpp`: authentic serial polling, strobe latch, DMC-corruption of controller reads suppressed correctly
- [ ] T045 [P] [US4] Implement `setController`/`controller` full-replace API in `core/src/cutenes/system/machine.cpp` + `core/include/cutenes/controller.hpp` (Port One/Two isolation, US4 scenario 2)
- [ ] T046 [US4] Add input-injection playtest scenario in `tests/harness/playtest.cpp`: per-frame button scripts reach gameplay on a known menu-driven title

**Checkpoint**: AI/automation can play, not just watch.

---

## Phase 7: User Story 5 - Save States and Deterministic Restore (Priority: P5)

**Goal**: Mid-frame capture/restore with byte-identical continued execution (spec US5, FR-011/FR-012).

**Independent Test**: `rom_harness savestate --at-frame 1000 --rerun-to 2000` — restore-and-rerun byte-identical across >= 10 save/restore cycles (US5 independent test, SC-006).

### Tests for User Story 5 (write first, verify RED)

- [ ] T047 [P] [US5] Write `tests/unit/test_save_state.cpp`: header round-trip ("CTNS" magic, 24-byte header, formatVersion, CRC-32), unknown-section skip within a version, every `LoadStateError` path, never-partially-apply property

### Implementation for User Story 5

- [ ] T048 [US5] Implement blob codec in `core/src/cutenes/serialize/save_state.cpp`: little-endian fixed-width, per-component tagged sections ("CPU ", "PPU ", "APU ", "RAM ", "MAP ", "INP ", "SCH ", "SRAM") with length prefixes, rebuild-derived-tables-on-load (contracts/save-state-format.md)
- [ ] T049 [US5] Implement component serializers in each component (`core/src/cutenes/{cpu,ppu,apu,bus,mappers,input,system}/`): PPU dot/scanline + shift registers + secondary OAM + evaluation phase; APU frame-counter step + "pending $4017 write latch"; DMC arbitration phase; MMC1/MMC3 latches (US6 mappers slot in via the same interface); master clock + pending events as absolute deltas (research D9)
- [ ] T050 [US5] Implement SRAM API in `core/src/cutenes/cartridge/sram.cpp` + `core/include/cutenes/save_state.hpp`: `sram()`/`loadSram` raw byte ranges, size from header/mapper ("iNES flags-8 value 0 means 8 KiB for compatibility"; NES 2.0 logarithmic encoding) (research D10)
- [ ] T051 [US5] Implement savestate scenario in `tests/harness/savestate.cpp`: capture@1000 → rerun-to-2000 comparison, repeated-restore stability, cross-cartridge rejection (SC-006)
- [ ] T052 [US5] Verify version rejection: a blob from a different core build refused with `IncompatibleCoreVersion` (US5 edge case, quickstart scenario 5)

**Checkpoint**: Deterministic branching — the enabler for rewind, TAS tooling, and AI experimentation.

---

## Phase 8: User Story 6 - Broad Commercial Library Support (Priority: P6)

**Goal**: Mappers 1/2/3/4 run commercial games correctly; unsupported mappers rejected by name (spec US6, FR-006).

**Independent Test**: Curated playtest list spanning mappers 0-4 each reaches gameplay; an unsupported-mapper ROM exits with a named `UnsupportedMapper` error (US6 scenarios).

### Implementation for User Story 6

- [ ] T053 [P] [US6] Write `tests/unit/test_mappers.cpp` (bank-switch truth tables per mapper), then implement MMC1 in `core/src/cutenes/mappers/mmc1.cpp`: 5-bit shift-latch write protocol, PRG modes (32K/16K-fixed-low/16K-fixed-high), CHR 4K/8K banks, mirroring control, SRAM write protection, MMC1A/B variants
- [ ] T054 [P] [US6] Implement UxROM in `core/src/cutenes/mappers/uxrom.cpp`: 16K bank switch + fixed last bank, bus-conflict behavior (AND/OR/NOMATCH subtypes), CHR-RAM
- [ ] T055 [P] [US6] Implement CNROM in `core/src/cutenes/mappers/cnrom.cpp`: 8K CHR bank switch, mirroring from header, bus-conflict behavior
- [ ] T056 [US6] Implement MMC3 in `core/src/cutenes/mappers/mmc3.cpp`: PRG $8000/$A000 bank modes (R6/R7), CHR 2Kx4 + 1Kx8 modes, A12-rise IRQ clocking (12 PPU-cycle guard), IRQ counter reload/enable/lock, MMC3 rev A/B, MMC6 SRAM differences documented
- [ ] T057 [US6] Drive mmc3_test + mmc3_test_2 to pass via the T031 harness (tier-1 completion)
- [ ] T058 [P] [US6] Add tier-2 mapper verification: Holy Mapperel builds (mappers 1/2) from `third_party/holy-mapperel/` + rainwarrior 2_test/3_test bus-conflict ROMs in `tests/harness/suite.cpp` behind the `tier2` label (golden-frame or manual checklist, research D11)
- [ ] T059 [US6] Create curated playtest list `tests/roms/playtest.txt` + `rom_harness playtest` execution (one representative commercial title per mapper 0-4 with scripted boot-to-gameplay criteria, US6 scenario 1)
- [ ] T060 [US6] Verify unsupported-mapper rejection names the id in `detail` end-to-end via harness (US6 scenario 2)

**Checkpoint**: The emulator is useful for real gaming, not just tech demos.

---

## Phase 9: Polish & Cross-Cutting Concerns

**Purpose**: Performance proof, documentation, release hygiene.

- [ ] T061 Implement bench scenario in `tests/harness/bench.cpp`: sustained fps measurement, single-threaded; document >= 120 fps NTSC ("2x headroom", SC-003) on a reference machine
- [ ] T062 [P] Run full `quickstart.md` validation (scenarios 1-7) on Windows, macOS, Linux; record results in `specs/001-emulation-core/` alongside golden hashes
- [ ] T063 [P] Write consumer documentation in `README.md` + `docs/core-usage.md`: CMake consumption (FetchContent + find_package), sink implementation example, save-state/SRAM handling, linking to contracts/
- [ ] T064 Clean cross-cutting warnings: `-Wconversion` clean, UBSan job clean, warnings-as-errors on all 3 toolchains (`.github/workflows/build.yml` + `core/CMakeLists.txt`)
- [ ] T065 Final determinism audit: cross-platform golden-hash equality on all 3 CI OSes for the T028 baseline titles; tag `core-v0.1.0`

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: no dependencies — start immediately
- **Foundational (Phase 2)**: depends on Phase 1 — BLOCKS all stories
- **US1 (Phase 3)**: depends on Phase 2 — the MVP
- **US2 (Phase 4)**: depends on **US1** (needs a running machine + harness)
- **US3 (Phase 5)**, **US4 (Phase 6)**: depend on US1; parallel with each other and with US2
- **US5 (Phase 7)**: depends on US1 (serializer touches all components — lands best after US2 accuracy fixes so serialized state is final-form)
- **US6 (Phase 8)**: depends on US1 (registry) + US2 T031 (harness) for MMC3 tier-1 tests
- **Polish (Phase 9)**: depends on all stories

### User Story Independent-Test Criteria

- **US1**: determinism run-twice, zero diffs, headless in CI
- **US2**: `ctest -L tier1` green on 3 OSes
- **US3**: stepped session hash-identical to uninterrupted run
- **US4**: scripted input reaches gameplay via authentic polling
- **US5**: save@1000/rerun-to-2000 byte-identical, 10x
- **US6**: playtest list green; unsupported mapper named-and-rejected

### Parallel Opportunities

- Phase 1: T003-T007 all [P]
- Phase 2: T013 parallel with T008-T012
- US1: T014-T017 test files all [P]; T018-T025 component implementations largely [P]-able in pairs once contracts are fixed (different files: cpu/, ppu/, apu/, cartridge/, mappers/)
- US3-US4 phases: all [P] tasks across the two stories can run concurrently after US1
- US6: T053-T055 (MMC1/UxROM/CNROM) fully parallel; MMC3 sequential after

## Parallel Example: User Story 1

```text
After T003/T008-T012 (API + skeleton), launch in parallel (different files):
Task: "T018 iNES/NES 2.0 parser" (core/src/cutenes/cartridge/ines.cpp)
Task: "T019 CPU documented set" (core/src/cutenes/cpu/decoder.cpp)
Task: "T021 PPU background pipeline" (core/src/cutenes/ppu/ppu.cpp)
Task: "T024 APU + mixer + downsampler" (core/src/cutenes/apu/)
Then integrate: T026 frame delivery -> T027 determinism -> T028 golden baseline.
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Phases 1-2 (setup + foundation)
2. Phase 3 (US1) → **STOP and VALIDATE**: headless deterministic NROM gameplay in CI
3. This alone delivers value: any consumer can run games and capture output

### Incremental Delivery

1. Foundation → US1 MVP (commit + tag baseline)
2. US2 accuracy gate (every later change is now protected by the suite)
3. US3 + US4 in parallel (automation surface)
4. US5 save states → US6 mappers → Polish/audit → v0.1.0

### Notes

- Constitution Principle III: test tasks are written FIRST and verified failing (RED) before their implementation task
- Commit after each task or logical group; checkpoints are natural validation/commit points
- Every accuracy-sensitive task cites its research.md decision — keep the citations beside the code
