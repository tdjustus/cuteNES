# Core internals

How the emulation core (`core/`) models the NES: time, the CPU, the PPU,
the APU, cartridges, save states, and what keeps it deterministic. Read
[`architecture.md`](architecture.md) first for where the core sits, and
[`using-the-core.md`](using-the-core.md) for its public API.

The decisions and their sources are in feature 001's
[`research.md`](../specs/001-emulation-core/research.md) (D1 to D12), and
the timing analyses in
[`analysis/`](../specs/001-emulation-core/analysis/). Hardware behavior
comes from the [NESdev wiki](https://www.nesdev.org/wiki/NES_reference_guide).

## Source map

| Directory | What |
|---|---|
| `core/include/cutenes/` | the public API (`cutenes.hpp`, `version.hpp`) |
| `core/src/cutenes/system/` | `Machine` and its implementation, the `Scheduler` (master clock), region profiles, `timing.hpp` |
| `core/src/cutenes/cpu/` | the 2A03's 6502 core |
| `core/src/cutenes/ppu/` | the 2C02 PPU |
| `core/src/cutenes/apu/` | the APU: five channels, the mixer, the resampler |
| `core/src/cutenes/bus/` | the CPU bus: RAM, mirroring, open bus, device decoding |
| `core/src/cutenes/cartridge/` | iNES and NES 2.0 parsing, the cartridge |
| `core/src/cutenes/mappers/` | the mapper interface and every board ([`mappers.md`](mappers.md)) |
| `core/src/cutenes/serialize/` | the save-state codec |
| `core/src/cutenes/cheats/` | Game Genie decoding |

## Time: one master clock

**General.** Give the whole machine one clock, counted in integer ticks of
the fastest oscillator, and express every component's rate as a whole
number of those ticks. Nothing reads the host's clock, and no rate is a
floating-point number, so time is exact and identical everywhere.

**For the NES.** The `Scheduler` (`system/scheduler.hpp`) holds a single
monotonic 64-bit master-clock counter, the only time source in the core
(research D2):

| | NTSC | PAL |
|---|---|---|
| Master ticks per CPU cycle | 12 | 16 |
| Master ticks per PPU dot | 4 | 5 |
| PPU dots per scanline | 341 | 341 |
| Scanlines per frame | 262 | 312 |
| Frames per second | 60.0988 | 50.0070 |

Rates are integer rationals in `system/region.hpp`, each from the NESdev
cycle reference chart. Timing constants that are not plain rates (the VBL
edges, the NMI sample point, the odd-frame skip, the frame sequencer's
steps) are named in `system/timing.hpp`, with their sources. A new
offset anywhere else is not allowed: changing one is a behavior change and
has to be justified by the tests it moves.

## Catch-up: the CPU drives, the others follow

**General.** Rather than step every component in lockstep, or run them on
threads, let the component that makes bus accesses run ahead, and bring
the others up to the current time just before each access that could see
them. This is as exact as lockstep at the points where it matters, and far
cheaper.

**For the NES** (research D1). The CPU is an instruction-level 6502 with
exact per-access cycle costs, including dummy reads and page-cross
penalties. Each CPU cycle is advanced in two halves around its bus access,
and at each access the PPU and APU are run up to the current master tick
(`runTo`), so a register read sees exactly the dot it lands on.

- The **PPU** is stepped dot by dot. It lags the CPU by one cycle from
  power-on (the hardware's reset synchronizer), so `runTo(M)` runs the dots
  that end by `M - 12`.
- The **APU** is stepped per CPU cycle.
- The **NMI** line is sampled once per CPU cycle, a set number of ticks
  before the cycle ends (`kNmiSampleLeadTicks`), with the PPU caught up to
  that instant. Every kind of cycle (read, write, internal) samples at the
  same point. Getting that last part wrong was the cause of #42; the
  analysis is in
  [`STATUS_READ_MODEL.md`](../specs/001-emulation-core/analysis/STATUS_READ_MODEL.md).
- **IRQ** is sampled after the first half of each cycle.
- **DMA** (sprite and DMC) is run as whole CPU cycles of the region's
  length, alternating get and put cycles as the wiki's DMA page describes;
  a DMC fetch can steal an OAM get.

The whole core is single-threaded. Threading its components (as higan and
ares do) was rejected: no accuracy gain on the test suites, and a risk to
determinism.

## The CPU

`cpu/cpu.cpp` implements every opcode, documented and unofficial, with the
2A03's quirks: no decimal mode (the D flag is kept but ignored by ADC and
SBC), the documented power-on and reset states (research D4), and the
dummy reads and writes real hardware makes. Interrupt polling, the
NMI/IRQ hijack of BRK, and branch timing follow the wiki and the
`cpu_interrupts_v2` and `branch_timing_tests` ROMs.

## The PPU

`ppu/ppu.cpp` steps one dot at a time through the 341 x 262 (or 312) frame:
background fetches, the shift registers, sprite evaluation into secondary
OAM, sprite-0 hit and overflow as blargg's tests require (research D3),
the odd-frame dot skip, and the documented open-bus decay of its data
latch. It notifies the mapper of A12 rises for MMC3-class scanline IRQs.

Its output is a 256x240 frame of palette indices with emphasis bits, which
the machine turns into colors through a 512-entry table built from the
selected palette.

## The APU

`apu/apu.cpp` steps the two pulse channels, the triangle, noise and the DMC
per APU cycle, with the frame sequencer at the cycle counts in
`timing.hpp`. Mixing uses the wiki's lookup tables (31 pulse entries, 203
for triangle, noise and DMC), computed once at start-up; at run time it is
integer adds and a clamp (research D8). A fixed-point resampler in the core
turns the native-rate samples into int16 mono at the rate the consumer
asks for (44.1, 48 or 96 kHz), so every consumer gets the same samples.
Matching the sound card's clock is the consumer's job (the desktop
application's dynamic rate control, in `architecture.md`).

## Cartridges and mappers

`cartridge/ines.cpp` parses iNES 1.0 and NES 2.0 headers (PRG/CHR sizes,
RAM sizes, the TV system, submappers) and creates the board with
`createMapper`. A `Mapper` (`mappers/mapper.hpp`) decodes the cartridge's
part of the CPU bus, the PPU's pattern bus, and nametable mirroring, and may
raise an IRQ. Boards with more (MMC5's registers, ExRAM and nametable
sources; MMC1's banked WRAM; PRG-RAM protection) override the hooks for
them. Revision differences (MMC1A/B, MMC3A/B) are in
[`mappers.md`](mappers.md).

## Save states

A save state is a versioned binary blob (research D9;
`serialize/save_state.hpp`):

- a 24-byte header: the magic `CTNS`, the format version, flags, the core
  version, the region, the cartridge id and a CRC-32 of the sections;
- tagged sections, each a 4-byte tag, a 4-byte length and its payload:
  `CPU `, `PPU `, `APU `, `RAM `, `MAP ` (the mapper), `INP ` (controller
  shift registers), `SCH ` (the master clock), `ROM ` (CRC-32s of PRG and
  CHR, so a state loads only into its own game, #92) and `SRAM`.

Everything is little-endian with explicit widths. All sub-cycle state is
saved, down to the PPU's shift registers and pending latches, so a state
taken mid-frame resumes exactly. Tables that can be rebuilt are rebuilt,
not saved. A load is validated completely before anything is applied, and
unknown section tags are skipped. Until a stable release declares a
compatibility window, a state loads only into the exact core version that
wrote it (`IncompatibleCoreVersion`); that is why the core's version
changes only when the core is released
([`source-control.md`](source-control.md), "Versions and tags").

## Determinism

The rules (feature 001's data model, "Integrity rules"):

1. No global mutable state: everything lives in a `Machine`.
2. The master clock is the only time source; nothing reads a wall clock.
3. Integer arithmetic only in emulation paths. Floating point is allowed
   only to build tables once at start-up.
4. All serialization is little-endian with explicit widths.

The compilers back these up (research D7): `-ffp-contract=off` on GCC and
Clang, `/fp:strict` on MSVC, `-Wconversion`, and fixed-width integer types
in all serialized state. Integer-only audio matters here: a `double` mixer
gives the same output for one build but not across compilers, and cuteNES
promises byte-identical audio across all three platforms. The `behavior`
tests run ROMs twice and compare, and CI's golden hashes compare across
platforms.

## Debugging the core

- `rom_harness` runs a ROM to its verdict, saves frames, and checks
  determinism and save states ([`building-and-testing.md`](building-and-testing.md)).
- `Machine::dumpMemory` and `memoryDump` give the whole memory map at once.
- The core's trace hook (`TracePoint`) reports every instruction with the
  PPU's position; `core.trace` and the desktop's trace logger write it in
  nestest.log's format.
- The timing analyses in
  [`analysis/`](../specs/001-emulation-core/analysis/) record how earlier
  timing bugs were found, including every fix that was tried
  (`FIX_ATTEMPTS.md`).
