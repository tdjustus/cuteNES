# `$2002` status-read and NMI model: redesign (issue #42)

**Status:** design, not started. The maintainer has scheduled it after the MCP
server (#31) and the Qt frontend (#32), and it must land before 1.0.

**Why:** `vbl_nmi_timing/7.nmi_timing` fails ("NMI 2 PPU clocks too early").
The 2026-10-29 attempt (`FIX_ATTEMPTS.md`) showed the cause is not where NMI
is sampled. No NMI delay, NMI sample point or Mesen2-style access split
passes the test. The ROM aligns the CPU to the PPU with `$2002` polling
loops, and in this core those three alignments do not land one PPU clock
apart as they do on hardware. The `$2002` read model is a set of separately
calibrated patches, and they disagree with one another about when a flag
change becomes visible.

## Hardware rules (the target)

From nesdev "PPU frame timing" and "PPU registers":

1. For the reference alignment, "a read will see a change to a flag if and
   only if it starts at or after the PPU tick where the flag changes". Other
   alignments may see it one tick earlier or later. A CPU cycle can start
   0-3 master ticks before the nearest following PPU tick.
2. "If $2002 is read as clear one PPU clock before a VBL, the flag is not
   set and the NMI is not generated for that frame."
3. "If $2002 is read as set on the same PPU clock or one after a VBL, the
   flag is then cleared, but the NMI for that frame is still not generated."
   The cause is "the $2002 read pulling the NMI line back up too quickly
   after it drops", and "CPU inputs (like NMI) are sampled each clock".
4. Reads two or more PPU clocks before or after the set behave normally.
5. The flag clears at (261, 1), 6820 PPU clocks after the set.
6. NMI is serviced after the current instruction if the edge was latched by
   the second-to-last cycle (already modelled; `Cpu::step`).

The rules are continuous. Visibility depends only on the master time of the
read and the master time of the change. No rule mentions "previous dot",
"same dot" or a kill window.

## Current model (what gets replaced)

| Piece | Where | What it does |
|---|---|---|
| `m_statusLag` | `Ppu::stepDot`, `Ppu::cpuRead` | Reads see the status as of the previous dot |
| Asymmetric-lag branch | `Ppu::cpuRead` | Reverses the lag for a set that happened earlier in the same dot (comment: sprdma boot poll) |
| Race window `kVblRaceDotMax = 2` | `timing.hpp`, `Ppu::cpuRead` | A read at dots 0-2 of 241 returns the flag clear |
| Kill window `kNmiKillDotMin/Max = 1..4` | `timing.hpp`, `Ppu::cpuRead`, `consumeNmiKill` | A read at dots 1-4 cancels an NMI that is already latched |
| Pre-read NMI sample | `Cpu::rd` (`$2002` only) | Samples NMI before the read (comment: Holy Mapperel vsync hang) |
| NMI sample after the access | `Cpu::rd`/`wr` | NMI is sampled mid-cycle on bus cycles, at the cycle end on internal ones |
| `m_suppressSet` | `Ppu` | Cleared and saved but never set: dead state |
| PPU lag of 12 ticks | `Ppu::runTo` | Reset synchronizer: PPU state reflects `master - 12` |

The kill window (4 dots) is longer than the hardware's (0-1 clocks). It
compensates for quantization in the other pieces. That is the signature of
patches calibrated against one another rather than against rule 1.

## Target model

**One visibility rule for every `$2002` bit.** The PPU keeps the status byte
together with the master time of its last change and the value before that
change:

```
status, statusPrev, statusChangedAt
read at master R sees: R >= statusChangedAt + kStatusReadLatency ? status : statusPrev
```

This replaces `m_statusLag` and the asymmetric branch, and it applies the
same way to VBL, sprite-0 and overflow, for both set and clear. Each flag
change records `statusChangedAt` at its dot's master time. Two changes inside
one latency window can't happen: the flags change at different dots, many
cycles apart.

**The read happens at an exact master time.** `R = cycleStart +
kCpuReadOffset`, with the PPU caught up to `R` before the read. The offset is
a named, region-parameterized constant in `RegionProfile`, not the implied
half cycle in `tickHalf`. Writes get `kCpuWriteOffset`.

**Suppression is derived, not windowed.**
- Rule 2: a read in `[set - 1 dot, set)` arms "do not set this frame". This
  is what `m_suppressSet` was meant to do; wire it up or remove it.
- Rule 3: the CPU samples the NMI line once per cycle at `cycleStart +
  kNmiSampleOffset`. A read that clears the flag before the next sample
  leaves the line high at that sample, so no edge is latched. This replaces
  the kill window, `consumeNmiKill` and `m_nmiKilled`.
- Check that the pre-read sample in `Cpu::rd` is subsumed by this. The
  vsync-hang case it fixed is a poll loop whose read always clears the flag
  before the next sample; with a sample offset before the read offset the
  edge is seen. Delete it only if Holy Mapperel still passes.

**Parameters, calibrated once.** Everything that remains is a named constant
in `timing.hpp`/`RegionProfile`:

| Constant | Meaning | Search range |
|---|---|---|
| `kCpuReadOffset` | Master tick of a read within the CPU cycle | 0-11 (NTSC) |
| `kCpuWriteOffset` | Master tick of a write | 0-11 |
| `kNmiSampleOffset` | Master tick of the per-cycle NMI sample | 0-11 |
| `kStatusReadLatency` | Ticks from a flag change until a read sees it | 0-4 |
| `kCpuPpuAlignment` | Master offset of the PPU dot grid against the CPU cycle (rule 1: 0-3) | 0-3 |
| PPU reset lag | Existing 12 ticks | keep unless the sweep says otherwise |

PAL uses its own dividers (16 per CPU cycle, 5 per dot), so its offsets are
separate values, calibrated against `pal_apu_tests` and any PAL `$2002` ROM.

## Work plan (test-first)

1. **Unit tests from the rules**, written before any change. Add a
   `tests/unit/test_ppu.cpp` fixture that drives a `Ppu` and reads `$2002`
   at exact master times around (241, 1) and (261, 1). Assert rules 1-5:
   - a read one dot before the set returns clear, the flag never sets that
     frame and there is no NMI;
   - a read on the set dot or one after returns set and there is no NMI;
   - a read two dots away behaves normally;
   - the clear is symmetric.

   Add a CPU-level test for rule 3 in `tests/unit/test_cpu.cpp`. These tests
   use dot offsets, not the calibrated constants, so they hold for any
   calibration.
2. **Sweep harness** (committed under `tests/harness/`, not a scratch
   binary). It runs a fixed ROM list in-process for each parameter tuple and
   prints a pass/fail matrix. The constants become runtime-overridable only
   in a test build (`CUTENES_TIMING_SWEEP`), never in the shipped core.
   Prune first with the fast discriminators (7.nmi_timing, ppu_vbl_nmi
   02/03/05/07), then confirm surviving tuples against the full gate.
3. **Implement the target model** behind the constants. Delete
   `m_statusLag`, the asymmetric branch, `kVblRaceDotMax`, `kNmiKill*`,
   `consumeNmiKill`, `m_nmiKilled` and `m_suppressSet` (if unused).
4. **Calibrate.** Choose one tuple that passes the whole gate, and record the
   matrix in `FIX_ATTEMPTS.md`. If several tuples pass, prefer the one that
   matches rule 1's reference alignment and Mesen2's defaults (read 5,
   write 7, PPU offset 1), and say why.
5. **Save-state format.** The PPU and CPU sections change, so bump
   `formatVersion` to 2 in `Machine::Impl::applyState`/`snapshot`. States are
   already rejected across core versions (`IncompatibleCoreVersion`), so no
   migration code is needed before 1.0.
6. **Re-pin.** Every golden hash and `expect-hash` playtest that moves must be
   viewed and explained before it is re-pinned (`docs/test-corpus.md`). A
   moved pin with no explanation counts as a regression.
7. **Docs.** Update `TIMING_MODEL.md` (inventory items 1-4) and
   `docs/test-corpus.md`, move the entry from `corpus-known-failing.txt` to
   `corpus.txt`, and remove the "Documented limitation" section.

## Gate (all must pass)

- `ppu_vbl_nmi` 01-10 (tier 1), `vbl_nmi_timing` 1-7 (corpus),
  `cpu_interrupts_v2` 1-5, `branch_timing_tests` 1-3
- `ppu_open_bus`, `ppu_read_buffer`, `sprite_hit_tests`, `sprite_overflow_tests`
  (sprite-0 and overflow bits share the read path)
- `apu_test`, `blargg_apu_2005` 1-11, `pal_apu_tests` 1-10 (the read and
  write offsets move APU register timing as well)
- `dmc_dma_during_read4`, `sprdma_and_dmc_dma`, `cpu_dummy_reads`,
  `mmc3_irq_tests`, `mmc3_test_2`
- Holy Mapperel (vsync poll) and the full playtest list, including the
  `roms-local/` commercial entries where they are available
- golden hashes (after viewing), determinism, save-state round trip, and the
  benchmark (2x real time locally)

## Stopping rule

Steps 1-4 are one design pass. If no tuple passes the gate, keep the unit
tests from step 1 (they are correct whatever the outcome), revert the model,
record the matrix and report to the maintainer before trying a second
design. Do not widen a window or add an alignment-specific special case to
make one ROM pass: that recreates the patchwork this design replaces.

## Risks

- `03-vbl_clear_time` and the sprdma boot alignment were each fixed by one
  of the patches being removed. They are the most likely to regress, so run
  them first in the sweep.
- APU timing tests are sensitive to the write offset. If a `$4017`
  write-time constant (`kFrameIrq*`) has to move, that is a separate
  calibration and a separate commit in the same PR.
- The sweep space (about 12 x 12 x 12 x 5 x 4 tuples) is too large to search
  exhaustively against the full gate. Prune with the hardware constraints
  first: read and write offsets inside the M2-high part of the cycle, and
  NMI sampled once per cycle.
