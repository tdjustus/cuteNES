# Contract: Save-State Blob Format

**Feature**: 001-emulation-core | **Authority**: FR-011, FR-012; research D9

All multi-byte fields are little-endian, fixed-width. The blob is opaque to
consumers; this document is the contract for tooling and debugging.

## Header

| Offset | Size | Field | Value |
|--------|------|-------|-------|
| 0 | 4 | magic | `"CTNS"` (0x43 0x54 0x4E 0x53) |
| 4 | 2 | formatVersion | starts at 1; bumps on ANY layout change |
| 6 | 2 | flags | bit 0: contains SRAM section; others reserved 0 |
| 8 | 8 | coreVersion | core release identifier (semantic version packed) |
| 16 | 1 | region | 0 = NTSC, 1 = PAL |
| 17 | 1 | reserved | 0 |
| 18 | 2 | cartridgeId | hash of (mapperId, prgSize, chrSize, mirroring) |
| 20 | 4 | payloadChecksum | CRC-32 of the sections area |

Total header: 24 bytes.

## Sections

After the header, a sequence of sections:

| Field | Size | Notes |
|-------|------|-------|
| sectionTag | 4 | `"CPU "`, `"PPU "`, `"APU "`, `"RAM "`, `"MAP "`, `"INP "`, `"SCH "`, `"SRAM"` |
| length | 4 | payload byte count |
| payload | length | component-private, but layout-versioned by formatVersion |

A reader skips unknown tags (forward compatibility within a formatVersion);
a writer of version N never writes a section layout a version-N reader
cannot parse.

## Completeness rules (what each section must capture)

Derived from the data-model completeness rule; mid-frame capture is required
(FR-011), so nothing may be omitted on the grounds of "rebuildable at a
boundary":

- **CPU**: A, X, Y, S, P (bit 5 materialized as 1; B never serialized),
  PC, cycle parity, NMI/IRQ lines incl. pending-edge state.
- **PPU**: register file + decay latch; dot, scanline, odd-frame flag;
  VRAM, palette RAM, OAM, secondary OAM + evaluation phase; background and
  sprite shift registers; fine-x latch; status flags + NMI suppress state.
- **APU**: all five channels' state (timers, envelopes, lengths, sweeps,
  sequencer positions), frame-counter step/mode/IRQ flags + **pending
  $4017 write latch**, DMC address/bytes/buffer/delta state, mixer
  accumulators, DMA arbiter state (halt/dummy/align/get/put phase).
- **RAM**: 2KB work RAM.
- **MAP**: mapper id + bank state (MMC1 shift latch, MMC3 IRQ counter/lock,
  mirroring override) + SRAM contents when battery (flag bit 0 set).
- **INP**: both controllers' shift registers + strobe state.
- **SCH**: master cycle (absolute), frame counter, any scheduled events as
  absolute master-clock deltas.

## Validation and rejection

`loadState` MUST reject, with the specific error, on:

1. bad magic ⇒ `BadMagic`
2. formatVersion != supported ⇒ `IncompatibleFormat`
3. coreVersion outside the supported window ⇒ `IncompatibleCoreVersion`
   (window = exact match until the first stable release; spec assumption)
4. cartridgeId mismatch ⇒ `WrongCartridge`
5. checksum mismatch ⇒ `ChecksumMismatch`

Never partially apply: validation completes before any state mutation.

## Determinism requirement

Restoring a blob and replaying an operation sequence MUST yield output
byte-identical to the uninterrupted run from the capture instant (FR-012,
SC-002/SC-006). Derived tables (mixer LUTs, downsampler filter state that is
a pure function of section data) are rebuilt on load, never serialized.
