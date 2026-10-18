# Research: NES Emulation Core

**Feature**: 001-emulation-core | **Date**: 2026-10-17

Four research streams (test-ROM suite & licensing, accuracy architecture,
build/test infrastructure, APU audio & save-state practice), resolved against
primary sources: the nesdev.org wiki and the actual sources of Mesen2, puNES,
ares, FCEUX, and the christopherpow/nes-test-roms archive. Every decision
below carries its rationale and rejected alternatives; claims trace to the
sources listed at the end of each entry.

## Decision Summary

| # | Topic | Decision |
|---|-------|----------|
| D1 | Emulation architecture | Hybrid: instruction-level CPU + dot-stepped PPU caught up at every CPU bus access (Mesen/puNES pattern) |
| D2 | Clock model | Single `u64` master clock; NTSC 12 ticks/CPU cycle, 4/PPU dot; PAL 16/5 |
| D3 | Sprite evaluation | Literal per-dot hardware state machine incl. the m-increment bug |
| D4 | Power-on/reset state | P=$24, S=$FD, A/X/Y=0; reset consumes 7 CPU cycles (golden-log exact); PPU writes ignored ~29658 cycles after reset |
| D5 | Test framework | GoogleTest, pinned FetchContent, tests-only |
| D6 | Build/export | CMake >= 3.21, root + `add_subdirectory(core)`, target `cutenes-core` + alias `cutenes-core::cutenes-core`, full install/export |
| D7 | Determinism | Integer-only emulation state; integer LUT audio mixer; `-ffp-contract=off`//`/fp:strict` safety net; UBSan CI job |
| D8 | Audio pipeline | One mixed sample per APU cycle (CPU/2 native); in-core fixed-point band-limited downsampling to a configurable output rate |
| D9 | Save states | Versioned binary blob, per-component sections, pending events at absolute master-clock deltas; reject on any mismatch |
| D10 | Battery SRAM | Raw cartridge-sized blob, separate from save states; consumer owns storage |
| D11 | Acceptance suite | Tier 1 (automated, memory-protocol ROMs) + Tier 2 (golden-frame/manual); ROMs via pinned submodule + zlib-licensed vendored set |
| D12 | CI | GitHub Actions 3-OS matrix (default toolchains), ccache/sccache, Linux UBSan job |

## D1 — Emulation architecture

**Decision**: instruction-level 6502 core with exact per-access cycle costs
(incl. dummy reads and page-cross penalties) + dot-stepped PPU advanced to the
exact master-clock boundary at every CPU bus access (twice per CPU cycle) +
APU stepped per CPU cycle. Strictly single-threaded.

**Rationale**: This is precisely what the two highest-accuracy open emulators
do (verified in source: Mesen2 `NesCpu.cpp` brackets every bus access with
`StartCpuCycle`/`EndCpuCycle` PPU catch-up; puNES `cpu_inline.h` ticks the PPU
at every bus cycle). It is the minimum level that passes the tier-1 suite:
`ppu_vbl_nmi` demands one-PPU-clock accuracy; `sprite_overflow_tests` fails
"an emulator with predictive overflow flag handling" (blargg's readme) and
tolerates only 1-2 CPU clocks of error. Community benchmarks put Mesen at
~273 fps on commodity hardware — 4.5x our 2x-headroom target.

**Alternatives rejected**:
- *Full component threading (higan/ares)* — equal accuracy, more complexity,
  risks determinism (nesdev forum t=8999 acknowledges the simplification is
  why higan chose threads); no test-suite gain.
- *Scanline PPU (FCEUX "Old PPU")* — cannot pass dot-timing suites at all.

**Key sources**: nesdev Catch-up page; Mesen2 `Core/NES/NesCpu.cpp`; puNES
`src/core/cpu_inline.h`; ares `ares/fc/cpu/cpu.cpp`; FCEUX docs.

## D2 — Clock model and constants

**Decision**: one `u64` master-clock counter is the sole definition of time.
NTSC profile: 12 master ticks per CPU cycle (6 per half-cycle), 4 per PPU dot
(3 dots per CPU cycle). PAL: 16/5. Dendy documented but out of scope. Master
clock 21,477,272.727 Hz = 236,250,000/11 (keep as integer ratio).

**Rationale**: nesdev Cycle reference chart and CPU pin-out ("divided by 12
(NTSC 2A03) or 16 (PAL 2A07)"); APU sequencer at CPU/2 (triangle every CPU
cycle). Register side effects land on exact dots; reads observe exact-dot
state (required by ppu_vbl_nmi's suppression-race tests).

**Critical constants** (all nesdev wiki, per-entry citations live in the
Phase 1 contracts and will live beside the code): 341 dots/scanline; 262
(NTSC) / 312 (PAL) scanlines; VBlank set (241,1), cleared (261,1); odd-frame
dot skip when rendering; OAM DMA 513/514 cycles; DMC DMA 3-4 cycles, DMC get
preempts OAM get; 29,780.5 CPU cycles/frame NTSC (29,780 2/3 rendering off);
60.0988 / 50.0070 fps; DMC rates 428..54 CPU cycles (NTSC).

**Disagreements registered**: DMC DMA cost has three framings in circulation
(APU_DMC "1-4", DMA page get/put 3/4, legacy "4+1") — follow the current DMA
page get/put model; PAL PPU dot behavior is under-documented (wiki TODO) —
PAL acceptance is per-frame/tier-level, not dot-level; master clock precision
21.477272 not 21.47727 MHz.

## D3 — Sprite evaluation and sprite-0 hit

**Decision**: implement the documented per-dot evaluation state machine
(dots 1-64 secondary-OAM clear, 65-256 odd-read/even-write, 257-320 sprite
fetches) including the buggy 9th-sprite diagonal scan (both n and m increment
on miss). Sprite-0 hit set at the output dot with all documented exclusions
(x=255, left clip, disabled layers, transparent pixels). No evaluation on
pre-render line.

**Rationale**: sprite_overflow_tests explicitly fails predictive
implementations and requires CPU-clock-precise timing; the diagonal scan is
the source of false positives games actually observe.

**Alternatives rejected**: scanline-batched evaluation (fails), prediction
(fails, called out by the test author).

## D4 — Power-on and reset state

**Decision**: power-on A=X=Y=0, S=$FD, P=$24 (bit 5 always reads 1; B flag
does not exist in the register — never materialize it), PC from $FFFC/$FFFD.
Reset: S -= 3, I=1, PC re-fetched; $4015 cleared, $4011 &= 1. The reset
sequence consumes 7 CPU cycles with the PPU ticking so CYC:7/PPU:(0,21)
matches the nestest golden log exactly. PPU writes to $2000/$2001/$2005/
$2006 are ignored for ~29,658 CPU cycles after reset (~33,132 PAL).

**Rationale**: nesdev CPU_power_up_state, PPU_power_up_state, and the first
line of nestest.log (`C000 4C F5 C5 ... P:24 SP:FD PPU: 0,21 CYC:7`).
Corrects the commonly-circulated "P=$34" (that value includes the phantom B
flag).

## D5 — Test framework

**Decision**: GoogleTest fetched via pinned FetchContent, inside `tests/`,
active only in standalone/dev builds.

**Rationale**: vendor-backed maintenance (doctest is in maintenance mode —
author's announcement, issue #554); CMake-native `gtest_discover_tests()`;
compile cost no longer a differentiator vs Catch2 v3. BSD-3 licensed, never
shipped in the MIT artifact.

**Alternatives rejected**: Catch2 v3 (valid runner-up; extra-module-path
discovery step), doctest (maintenance-mode risk), Qt Test (core is Qt-free
by constitution Principle II).

## D6 — Build and export

**Decision**: CMake >= 3.21. Root `CMakeLists.txt` with `add_subdirectory(core)`
plus guarded tests (never downloaded by consumers). Library target
`cutenes-core` with alias `cutenes-core::cutenes-core`, `target_compile_features
(cxx_std_20 PUBLIC)`, full `install(TARGETS ... EXPORT)` + `install(EXPORT)`
+ versioned package config. Consumers (Qt GUI, MCP server) attach via
FetchContent or find_package with the identical target name.

**Rationale**: dual-mode consumability (foonathan's FetchContent-preparation
pattern; CMake importing/exporting guide); 3.21 matches Qt 6.2+ requirements
so the later GUI consumes without friction; zero-dependency core means a
trivial Config file.

## D7 — Cross-platform determinism

**Decision**: integer-only emulation state and integer-only audio path are
the *mechanism*; compiler flags are the safety net: `-ffp-contract=off`
(GCC/Clang), `/fp:strict` (MSVC), `-Wconversion`; fixed-width `<cstdint>`
types everywhere in serialized state (never bare `char`/`long`); no
transcendentals in the core. UBSan + `-fno-sanitize-recover` job on Linux CI;
macOS arm64 runner provides ARM-vs-x86 coverage for free.

**Rationale**: verified in Mesen2 source that Mesen mixes audio in `double`
and keeps state integer — acceptable for Mesen because audio never feeds back
into emulation state, but **insufficient for cuteNES**: spec FR-012 requires
byte-identical audio and CI runs golden comparisons across three
platforms/compilers, where double arithmetic is not stable. The nesdev APU
Mixer page provides integer-friendly LUT formulations, making a fully integer
mixer exact and cheap.

**Alternatives rejected**: double mixer (Mesen-style) — per-build identical
but not cross-compiler; float state anywhere — excluded outright.

## D8 — Audio pipeline

**Decision**: the mixer computes one mixed integer sample per APU cycle
(CPU/2 = 894,886.4 Hz NTSC native — the DAC update rate; there is no "~53 kHz
native NES rate"). The core owns down-conversion to a consumer-configured
output rate (44.1/48/96 kHz) via a deterministic fixed-point band-limited
synthesizer (blip-buffer class). Tables (31-entry pulse, 203-entry TND) are
precomputed once at init — double math allowed at init only — runtime mixing
is int32 adds + clamp. Post-mix filters (HPF 90 Hz, HPF 440 Hz, LPF 14 kHz)
as fixed-point one-pole IIRs. Mono int16 output. Clock-drift compensation
(Mesen's ±0.25% dynamic rate) is a consumer-side concern, off by default.

**Rationale**: consensus of ares (per-APU-tick samples), Mesen (delta
band-limited synthesis in-core at 96 kHz), and nesdev forum guidance ("every
APU emulator effectively generates sound at the 1.79 MHz NES clock rate then
resamples"). Core-owned resampling keeps the GUI and MCP consumers identical
by construction; integer fixed-point keeps it byte-identical cross-platform.

**Alternatives rejected**: consumer-side resampling (consumers diverge —
violates parity/determinism), fixed per-frame sample counts (NES frame is not
exactly 1/60 s — drifts).

## D9 — Save states

**Decision**: versioned binary blob: magic + format version + core version
identifier + region + flags + checksum, then per-component sections each with
tag/length payload. Serialize *all* sub-cycle state: PPU dot/scanline +
shift registers + secondary OAM + evaluation phase, APU frame-counter step +
pending $4017 write latch, DMC arbitration flags, mapper IRQ counters, input
shift registers, and the master clock. Pending events stored as absolute
master-clock deltas. Derived/rebuildable tables are rebuilt on load, never
serialized. **Reject on any version mismatch** — no migration until a stable
release declares a compatibility window.

**Rationale**: Mesen's format (magic + version window + per-component
serialization incl. pending latches at absolute timestamps) demonstrates what
mid-frame capture requires; ares's unversioned positional serializer is the
anti-pattern its own docs warn about. Rejection is safest for automation
(spec assumes unstable format until first stable release); silent misalignment
is the one unacceptable failure.

## D10 — Battery SRAM

**Decision**: raw bytes, exact size from header/mapper (iNES flags-8 value 0
means 8 KiB for compatibility; NES 2.0 logarithmic encoding), exposed to the
consumer as load/save byte ranges. Never embedded in save-state sections
rewind needs except as its own versioned section. Consumer owns persistence
(one `.sav`-style file beside the ROM is the convention we document).

**Rationale**: raw PRG-RAM dumps are portable across emulators (nesdev
t=838); Mesen flushes battery on power-off and ROM switch — our consumer
performs the same at the same lifecycle points.

## D11 — Acceptance suite and ROM licensing

**Decision**: two tiers.

- **Tier 1 — automated CI gate** (memory-protocol reporting, headless):
  nestest (golden-log diff + $02/$03) → instr_test-v5 → instr_timing →
  cpu_interrupts_v2 → cpu_reset → ppu_vbl_nmi → oam_read → oam_stress →
  ppu_open_bus → sprdma_and_dmc_dma → apu_test → apu_reset → mmc3_test /
  mmc3_test_2.
- **Tier 2 — golden-frame or manual**: sprite_hit_tests, sprite_overflow_tests,
  blargg_ppu_tests_2005, branch/cpu timing sets, Holy Mapperel (mapper
  coverage for 1/2 + UxROM bus-conflict via rainwarrior 2_test/3_test).

Harness protocol: blargg consoles — signature `$DE $B0 $61` at $6001-$6003,
status at $6000 ($80 running, $81 reset-request honored with >=100 ms delay,
$00-$7F result), text at $6004; nestest automation — PC=$C000, results $02
(official) / $03 (unofficial), per-instruction log diff vs nestest.log.

**Licensing**: blargg/kevtris ROMs carry **no explicit license** (wiki-
endorsed archive, universally redistributed) → **pinned git submodule** of
christopherpow/nes-test-roms keeps unlicensed binaries out of the MIT tree.
Holy Mapperel is zlib-licensed → vendored under `third_party/` with its
LICENSE. `third_party/NOTICE.md` records per-item provenance. CI checks out
the submodule; no ROMs are distributed in releases.

**Premise corrections from research**: there is no `$F8` result convention on
NES (the real protocol is $6000/$6004); blargg produced no MMC1/UxROM/CNROM
functional tests — mapper 1/2/3 acceptance is Tier 2 (Holy Mapperel +
rainwarrior bus-conflict tests), mapper 4 is Tier 1 (mmc3_test), mapper 0 is
validated implicitly by every suite.

## D12 — CI

**Decision**: GitHub Actions matrix `ubuntu-latest` / `macos-latest` /
`windows-latest`, default toolchains (gcc / AppleClang / MSVC), Release +
Debug, Ninja generator, ccache (sccache variant on Windows) via
`CMAKE_CXX_COMPILER_LAUNCHER`, `ctest --output-on-failure`. Dedicated Linux
UBSan job (`-fsanitize=undefined -fno-sanitize-recover=all`). Test-ROM
submodule checked out in CI jobs.

**Rationale**: default runner toolchains give real-world compiler diversity;
compiler cache (not build-dir) caching is the reliable pattern; sanitizer
job catches the UB that would silently break determinism.
