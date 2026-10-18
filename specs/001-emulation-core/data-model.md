# Data Model: NES Emulation Core

**Feature**: 001-emulation-core | **Source**: [spec.md](spec.md)

This document defines the entities of the emulation core at design level.
Implementation types live in the code; this is the shared vocabulary that the
public API contract ([contracts/core-api.md](contracts/core-api.md)) and the
save-state format ([contracts/save-state-format.md](contracts/save-state-format.md))
are built on.

## Domain Entities (from spec)

### Cartridge

A loaded NES ROM image and its derived identity.

| Field | Type / Values | Notes |
|-------|---------------|-------|
| format | iNES 1.0, NES 2.0 | Detected from header; both parsed losslessly |
| mapperId | integer (0, 1, 2, 3, 4 supported initially) | Unsupported id → load rejected, id named in error |
| prgRom | byte data (256KB max iNES 1.0; NES 2.0 extended) | Program banks |
| chrRom | byte data, or chrRam when absent | Pattern data |
| mirroring | horizontal, vertical, four-screen, mapper-controlled | From header flags |
| battery | boolean | Enables 8KB (typical) persistent SRAM |
| region | NTSC, PAL, unspecified | From NES 2.0 flags when present |
| trainer | 512 bytes or none | Parsed and applied per format spec |

**Validation rules** (spec FR-001): invalid magic, truncated file, impossible
bank counts, and malformed NES 2.0 fields each produce a distinct rejection
error naming the defect. A valid cartridge with an unsupported mapper is
rejected separately with the mapper id.

**Relationships**: loaded into exactly one Machine; owns its Mapper instance
and SRAM when `battery` is true.

### Machine

The single emulation session; the aggregate root of the core.

| Field | Notes |
|-------|-------|
| cartridge | The loaded Cartridge, or none |
| region | NTSC or PAL; resolved at load (header override, else NTSC default) |
| runState | running, paused, needs-rom |
| masterCycle | Monotonic master-clock counter; the sole definition of "now" |
| frameCounter | Frames completed since power-on |

**State transitions**: `needs-rom --load()--> paused --run/resume--> running
--pause--> paused`; `reset()` reboots CPU state without unloading;
`powerOn()` returns to a fresh boot state. All transitions are consumer-driven
(spec FR-008); the core never self-schedules.

### VideoFrame

| Field | Notes |
|-------|-------|
| pixels | 256x240 deterministic RGB pixels (core default palette; consumer may remap) |
| frameIndex | Equals Machine.frameCounter at completion |

Delivered, complete, exactly once per frame boundary (spec FR-009). The pixel
representation is fixed so byte-identical comparisons are well-defined.

### AudioBuffer

| Field | Notes |
|-------|-------|
| samples | Signed 16-bit mono PCM at the configured output rate (44.1/48/96 kHz), produced deterministically from the native APU-cycle stream by the core's fixed-point band-limited downsampler (research.md D8) |
| startCycle | Master cycle of first sample; ties audio to frames deterministically |

Streamed to the consumer as emulated time advances (spec FR-010). Bounded per
delivery; the consumer drains.

### ControllerState

| Field | Notes |
|-------|-------|
| port | 1 or 2 |
| buttons | Bit set of {right, left, down, up, start, select, B, A} in serial-shift order |

Consumer sets the full state at any time; the machine serializes it through
authentic polling when software reads the input port (spec FR-007).

### RunCommand

The closed set of consumer operations on the Machine (spec FR-008):
`powerOn`, `reset`, `runFrames(n)`, `frameAdvance`, `stepInstruction`,
`pause`, `resume`. Single-stepping one instruction advances dependent
components to the exact cycle boundary of the next instruction.

### SaveState (MachineState snapshot)

Complete serialized machine: every component state below plus the master
cycle counter, at any instant including mid-frame (spec FR-011). Versioned
blob; rejected on format/core-version mismatch (see assumptions in spec).
Restore is byte-identical-deterministic (spec FR-012).

**Relationships**: captured from a Machine; restorable only into a Machine
holding the same cartridge.

## Internal Component State (defines save-state completeness)

These are the internal entities whose full state a SaveState must capture.
They are named here because "what must be in a save state" is a design
contract, not an implementation detail.

- **Cpu**: A, X, Y, S, P, PC; cycle counter; interrupt lines; pending NMI state.
- **Ppu**: register file; the PPU open-bus/latch byte; VRAM (2KB, per
  mirroring); palette RAM (32B); OAM (256B); secondary OAM (32B) and sprite
  evaluation progress; the two background shift registers and fine-x latch;
  current dot/scanline/odd-frame flag; VBlank/sprite0/overflow flags and the
  NMI line.
- **Apu**: per-channel state (pulse x2, triangle, noise, DMC) including
  timers, envelopes, length counters, sweep units, sequencer positions;
  frame counter mode/step/IRQ suppress/inhibit flags; DMC address/bytes-left
  and output level; mixer accumulators.
- **Bus/Ram**: 2KB work RAM contents.
- **Mapper**: bank select registers, CHR/PRG bank mappings, mirroring
  override, shift/write-latch state (MMC1), IRQ state (MMC3), and SRAM
  contents when battery is present.
- **Input**: both controllers' serial shift registers and strobe state.
- **Scheduler**: master cycle count; any pending scheduled event data.

**Completeness rule**: if a field influences future output and is not derived
from the above at reset, it belongs in the SaveState. The determinism
criterion (spec FR-012) is the arbiter: restore-then-run must equal
run-without-interruption, byte-identically, including audio.

## Persistence Beyond Sessions

- **Battery SRAM**: cartridge-scoped, survives power cycles and process exit;
  storage is consumer-provided (the core exposes load/save of SRAM contents,
  per core/UI separation — the core owns the format, the consumer owns the
  file). Size resolves from header/mapper (iNES flags-8 value 0 ⇒ 8 KiB
  compatibility default; NES 2.0 logarithmic encoding), per research.md D10.
- **SaveState**: consumer-stored opaque blob; the core validates on load.

## Integrity Rules

1. No global mutable state anywhere in the core; all state lives in a Machine
   instance (enables future multi-session servers, protects determinism).
2. The master cycle counter is the only time source; no wall-clock reads
   inside the core.
3. Integer arithmetic only in emulation paths; any floating point is confined
   to non-emulated helper math (see research.md determinism notes).
4. All multi-byte serialization is little-endian with explicit widths.
