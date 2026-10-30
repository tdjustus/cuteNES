# Unified timing model (design, pre-implementation)

Status: design. Supersedes the APU-only notes in `PHASE_MODEL.md`. Motivated by
the two accepted known failures (`03-vbl_clear_time`, `5-len_timing`) and the
MMC3 IRQ timing failures (`mmc3_irq_tests` 4 and 6), which all trace to the same
gap: sub-cycle ordering is hand-tuned per subsystem instead of derived from one model.

## 1. Goal

Every observable timing effect is a function of one master clock, through named
hardware events. No free-standing offset constant is allowed unless it names the
hardware behavior it models and cites its source.

## 2. What exists today

Master clock: `Scheduler::now()`. NTSC profile: 12 master ticks per CPU cycle
(`cpuDivisor`), 4 per PPU dot (`ppuDivisor`), APU step = 2 CPU cycles (24 ticks).

Current hand-set timing constants (inventory):

| # | Location | Constant | What it stands for | Status |
|---|----------|----------|--------------------|--------|
| 1 | `cpu.cpp` `tickHalf`/`rd`/`wr` | half-cycle = 6 ticks; access at mid-cycle | CPU phi2 sample point | Fixed by the design; the value is implied, not named |
| 2 | `cpu.cpp` `rd` | `$2002` pre-access `sampleNmi()` | NMI edge latched before the clearing read | Calibrated (comment: Holy Mapperel hang) |
| 3 | `ppu.cpp` `cpuRead` | `$2002` set-race window: dot <= 2 clears the flag; dot 1..4 kills NMI | VBL race (nesdev PPU_registers) | Calibrated |
| 4 | `ppu.cpp` `stepDot` | VBL set at (241,1); pre-render clear at (261,1) | VBL flag edges | Clear dot is the disputed value (see 03-vbl) |
| 5 | `ppu.cpp` ctor | ignore `$2000/$2001/$2005/$2006` for 29658 CPU cycles | post-reset PPU warm-up | Measured; keep, but name it |
| 6 | `apu.cpp` `stepApuCycle` (removed) / `k4`,`k5` | frame checkpoints 7457, 14913, 22371, 29830 | frame sequencer | 14913 is calibrated against `5-len_timing` tests 2/3 |
| 7 | `apu.cpp` `fire` | IRQ visible at APU boundary + 24 ticks; asserts until +30 | frame-IRQ flag visibility | Calibrated (`6-irq_flag_timing`) |
| 8 | `apu.cpp` `statusRead` | `frameIrqAt = now + 6` on re-assert | read-clears-flag race | Calibrated |
| 9 | `apu.hpp` `frameIrqPending` | `fire + 2 CPU cycles` | flag visibility | Calibrated |
| 10 | `apu.hpp` IRQ poll | `now + 24 >= frameIrqAt` | CPU IRQ line sample | Calibrated |
| 11 | `apu.cpp` `$4017` write | sequencer restarts at the write (immediate, no latency) | write-to-sequencer latency | Real hardware: 3 or 4 CPU cycles, parity-dependent (not modeled) |
| 12 | `apu.cpp` DMC | timer reload `rate/2 - 1` APU steps | DMC output period | Verified against `8-dmc_rates` |
| 13 | `apu.cpp` `$4015` write | DMC fill immediate, bit counter reset to 8 | DMC start semantics | Verified against `7-dmc_basics` |
| 14 | `mmc3.cpp` `clockIrq` | A12 clock semantics | MMC3 counter | Unit-tested; the clock *delivery* timing is not modeled |
| 15 | `cpu.cpp` DMC fetch | 3 stall ticks + read | DMC DMA halt | Approximate (T037) |

Pattern: the calibrated constants (2, 3, 6, 7, 8, 9, 10) are each correct for the
tests that were tuned against them. Together they are not derivable from one
ordering, which is why changing one breaks another.

## 3. Target model

**Events.** Everything that can be observed happens at a master-clock timestamp:

- CPU phi2 access at master `12k + s`, where `s` is a named sample offset (item 1).
- PPU dot `j` at master `4j` (already so; keep).
- APU step `n` at master `24(n+1)` (already so; keep).
- Frame-sequencer checkpoint `i` at `seqBase + 12 * cycles[i]`, where `seqBase`
  is the exact master time of the `$4017` write plus the hardware write latency (item 11).
- Frame-IRQ flag set and clear are events at checkpoint times plus named hardware offsets.
- A12 rises from the PPU are events at the dot where the pattern address changes.

**Rule.** Every reader (CPU read of `$2002`, `$4015`, `$2007`, IRQ line poll) sees
the state after exactly the events with timestamp <= its own sample time. No
subsystem computes its own "visible from" time with a private offset.

**Named offsets.** Each remaining offset is one entry in a single header
(`core/src/cutenes/system/timing.hpp`), with the hardware behavior and the test(s)
that pin it. Example shape:

```
// Frame IRQ flag is visible this many master ticks after the sequencer's
// step-4 event (nesdev APU Frame Counter; 6-irq_flag_timing rows 3-4).
constexpr std::uint32_t kFrameIrqVisibleTicks = 24;
```

**Derivation.** Each named offset is either (a) taken from published hardware
timing, or (b) the single value that satisfies the tests in its group. A group
that cannot be satisfied by one value is a model error, not a reason to add a
second constant.

## 4. Known open questions (decide with data, not by trial)

1. CPU phi2 sample offset within the cycle: 6 ticks (1.5 dots) today. Hardware
   value needs confirming against the `03-vbl_clear_time` row pattern and
   `07-nmi_on_timing`. This one value drives both PPU tests.
2. `$4017` write latency: 3 or 4 CPU cycles by APU parity. Affects item 6.
3. A12 delivery: whether the PPU's pattern-address edge is seen at the dot it
   occurs or at the next CPU sample. Affects `mmc3_irq_tests` 4 and 6.

Each of these is one parameter. The design is only complete when each has a
single value that makes all of its tests pass together.

## 5. Migration (each step keeps the gate green or is reverted)

1. **Inventory and naming (no behavior change).** Create `timing.hpp`; move the
   constants above into named values; add a test that the named values equal the
   current ones. Gate: identical behavior, all tests unchanged.
2. **Parameter sweep, offline.** For open questions 1-3, sweep the single parameter
   across its candidate range and record which tests pass per value, in
   `FIX_ATTEMPTS.md`. Pick the value that satisfies the largest consistent group.
   Gate: no code change yet.
3. **Derive IRQ visibility from the sequencer event.** Replace items 7-10 with
   offsets from the step-4 event. Re-run `6-irq_flag_timing`, `3-irq_flag`,
   `4-jitter`. Only land if all three pass.
4. **Apply the chosen sample offset and write latency.** Re-run the VBL, NMI,
   length, and MMC3 timing ROMs together.
5. **Stopping rule.** If step 4 cannot satisfy both groups with one value per
   parameter, stop and record which group conflicts. Do not add a per-test offset.

## 5a. Progress

- **Step 1 done** (no behavior change): constants moved to `core/src/cutenes/system/timing.hpp`
  and used at their sites in `apu.cpp`, `ppu.cpp`, `cpu.cpp`. Verified: frame hashes
  and audio sample counts for 9 ROMs identical before and after (captured with
  `rom_harness run ... 1200`); gate unchanged (133 of 135, the two known failures).
- Inventory correction: item 10 (IRQ poll `+ 24`) was in `core/src/cutenes/apu.hpp`, a
  stale duplicate header that the build does not use. The live header has no such
  constant. The stale file is tech debt to delete.

- **Step 2a done: CPU sample offset sweep** (S = 0..12 ticks into the cycle, all 133
  non-known tests). Only S = 6 and S = 7 leave every non-known test passing; neither
  fixes `03-vbl_clear_time`. S = 4, 5, 8, 10 and 12 each break at least one tier-1 test.
- **Step 2b done: sample offset x pre-render clear dot** (PPU/NMI set + `4-jitter`
  + `6-irq_flag_timing`, S in {0,2,4,5,6,7,8,10,12}, clear dot in {0,1,2,3}).
  Result: `03-vbl_clear_time` passes at clear dot 2 for every S. `07-nmi_on_timing`
  passes at clear dot 1 for every S. At clear dot 2, `07` passes only for S = 0 or 2,
  and then `10-even_odd_timing` and `6-irq_flag_timing` fail. No cell passes all of
  `03`, `07`, `10`, and `6-irq` together.
- **Conclusion:** the 03/07 conflict is over the pre-render VBL clear dot, and the CPU
  sample offset doesn't resolve it. The model needs another mechanism for how `07` sees
  the clear. The next analysis targets `07-nmi_on_timing` directly (what its rows
  measure against the pre-render clear), not more S values.
- Experiment hooks (`CUTENES_S`, `CUTENES_CLR`) were removed after the sweep. Baseline
  frame hashes match again.

- **Breakthrough (after the W sweep):** `03-vbl_clear_time` and `07-nmi_on_timing`
  disagree about the clear *only through the read path*. Model fix: `$2002` returns
  bit 7 from the previous dot's status (`m_statusLag`, latched at each dot start and
  synced by read side effects), while the NMI level uses the live flag. Clear dot
  stays 1. Result: all ten ppu_vbl_nmi ROMs pass; `03-vbl_clear_time` promoted to
  tier 1; gate 134/135 with only `5-len_timing` remaining. Serialized in the save state.

- **Sequencer redesign landed:** events at exact master times; step 4 split
  (29829 half-frame, 29830 IRQ); IRQ flag set quantized up to the APU grid
  (`kFrameIrqVisibleTicks = 0` now means grid quantization). Items 6/7/9/10 of the
  inventory are superseded by this model. Remaining open item: per-event-type write
  latency for 5-len_timing (see FIX_ATTEMPTS.md L11 notes).

- **COMPLETE:** all known failures fixed. The sequencer model: clock events from the
  write + 3-cycle restart skew, IRQ from the write grid-quantized, wrap as its own
  period-end event, 5-step period 37281.5 CPU cycles. No known-failing ROMs remain.

- **MMC3 IRQ resolved (T069):** revision B counter rule; the A12 delivery model
  (sprite-fetch rise at dot 262, BG fetch phases) was already correct — only the
  reload-clock assertion was missing. `4.Scanline_timing` and `6.MMC3_rev_B` gated.

## 6. Non-goals

- DMA alignment details beyond the existing approximation (T037), except as needed
  for the phi2 sample offset.
- Region variants (PAL) — the model is parameterized by the profile. PAL has
  since been added (see "Later additions").

## Later additions (after T069)

- **Half-frame clock vs register writes.** A write to a length-halt or
  length-load register in the cycle of, or the cycle before, a half-frame clock
  is simultaneous with it: the clock runs first, the new halt flag applies
  after it, and a length load is ignored if the counter was running at the
  clock (blargg apu 2005 tests 10 and 11). Clock event times are unchanged:
  moving them earlier fixes test 10 but breaks `5-len_timing`.
- **PAL frame counter.** 4-step clocks at 8313/16627/24939/33253 with period
  and IRQ at 33254 CPU cycles; 5-step ends at 41565. Noise and DMC period tables
  are the PAL ones. Selected from the region profile.
- **Taken branches and NMI.** The "poll before the second cycle" quirk of a taken
  non-page-crossing branch applies to the IRQ sample only; NMI is polled at the
  normal penultimate cycle (blargg Branch Timing Tests 3).
- **Open: NMI visibility at one-PPU-clock precision.** `vbl_nmi_timing/7`
  needs a different NMI-visibility threshold for each of its three CPU/PPU
  alignments; the core runs the CPU on whole three-dot cycles with a fixed
  phase, so no single rule passes all three (issue #42). A master-tick
  sweep and a Mesen2-style access split (FIX_ATTEMPTS.md, 2026-10-29) show
  that the cause is the `$2002` read model, not the NMI sample point.
