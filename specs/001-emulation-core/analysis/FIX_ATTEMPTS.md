# Known-failure fix attempts

Log of every attempted fix for the remaining known failures, with the outcome.
Check this file before trying a change so the same experiment is not repeated.

Remaining known failures (label `known-failing`):
- `ppu_vbl_nmi/03-vbl_clear_time`
- `apu_test/5-len_timing`

## ppu_vbl_nmi/03-vbl_clear_time (with 07-nmi_on_timing)

| # | Change | Result | Notes |
|---|--------|--------|-------|
| P1 | VBL clear at pre-render dot 1 (current) | 07 passes, 03 fails | Keeps all other tests green |
| P2 | VBL clear at pre-render dot 2 | 03 passes, 07 fails | Trade-off, not acceptable |
| P3 | VBL clear at pre-render dot 3 | 03 fails, 07 fails | Re-verified this session |
| P4 | `$2002` read sampled 1 half-cycle later (extra `tickHalf` before bus read, none after, `cpu.cpp` `rd`) | 4 tier1 NMI tests fail (05, 07, 08, 10) | Reverted. Shifts sample phase for all `$2002` reads globally |
| P5 | P4 + clear at dot 2 | 03 passes, 07 still fails, 4 tier1 NMI tests fail | Reverted |

Conclusion so far: moving only the clear dot is a zero-sum trade. Any fix has to
change the CPU-read-to-PPU phase, not the clear dot alone.

## apu_test/5-len_timing (test 2 vs test 3)

| # | Change | Result | Notes |
|---|--------|--------|-------|
| L1 | Frame checkpoint step 1 = 14913 (nesdev, current) | test 2 fails | Baseline |
| L2 | Step 1 = 14915 (k4 and k5) | test 2 fails | |
| L3 | Step 1 = 14916 | test 2 fails | |
| L4 | Step 1 = 14917, 14918, 14919 | test 2 passes, test 3 fails | Tests 2 and 3 are one CPU cycle apart; one constant cannot satisfy both |

| L5 | Constant +3 CPU offset on all frame-sequencer compares | 5-len still fails #2; 4-jitter and 6-irq_flag_timing break | Reverted. Shifts the free-running sequence too |
| L6 | +3 offset applied only after a `$4017` write (cleared on sequencer wrap) | 5-len still fails #2; 4-jitter and 6-irq_flag_timing break | Reverted |

Observation (trace this session): the sequencer advances in APU steps of 2 CPU cycles,
so every checkpoint lands on an even CPU offset. 14913 fires at 14914, 14916 stays
14916, 14917 fires at 14918. The window needed by tests 2/3 is an odd offset, so no
even-only checkpoint table can satisfy both. Hardware applies `$4017` writes 3 or 4 CPU
cycles late depending on APU parity (see the comment in `sync_apu.s`). Parity-dependent
start delay is the candidate model, but it needs a save-state field and must not
change the IRQ path (L5/L6 show the IRQ tests depend on the current start alignment).

| L7 | Parity-dependent start offset after `$4017` write (APU parity odd=3/even=4 CPU cycles) | 5-len still fails #2; 4-jitter and 6-irq_flag_timing break | Reverted. Clean rebuild verified (an earlier run had a header collision and was discarded) |
| L8 | Same, parity swapped (odd=4/even=3) | Same as L7 | Reverted |

| L9 | Phase model (PHASE_MODEL.md): frame sequencer scheduled in master ticks from the exact `$4017` write time; events interleaved with APU steps in `runTo`; IRQ visibility quantized to the APU boundary | 5-len still fails #2; 6-irq_flag_timing and 4-jitter break (code 03) | Reverted. Stopping rule hit. The IRQ calibration constants (+24, +30) assume APU-boundary timing |

Conclusion so far: neither a constant nor a parity-dependent start offset satisfies
tests 2 and 3, and both break the IRQ-timing tests. The remaining fix needs a
CPU/APU phase model shared by `$4017` writes and the frame sequencer, which is the
same work as T037/T068 and should be scoped as its own task.

## Mapper 4 (MMC3) — found with Mega Man 3

| # | Change | Result | Notes |
|---|--------|--------|-------|
| M1 | Fixed PRG mapping to the spec: mode 0 `$8000=R6, $C000=second-last`; mode 1 `$8000=second-last, $C000=R6`; `$E000=last` | Fixed. MMC3 tests still pass | `core/src/cutenes/mappers/mmc3.cpp` |
| M2 | PRG bank register mask `0x0F` → `0x3F` (MMC3 R6/R7 are 6-bit) | Fixed. Bank 16+ now reachable | Same file |
| M3 | MMC3 register select: odd `$8001` write took the register index from the data (`value & 7`) instead of the last `$8000` write (`mBankMode & 7`) | Fixed. Mega Man 3 no longer executes undocumented opcodes (first bad fetch gone over 80M instructions); rendering on | Same file. Found by stepping to the first undocumented opcode |
| M4 | Mega Man 3 'spin' at `$FEAD` was a false alarm: the game shows a static title screen until Start (nametable changes only after Start, verified). `$80-$8F` are written by the NMI each frame | Resolved (not a bug) | Playtest entry `mega-man-3-start-changes` presses Start, then checks screen change |
| M5 | Unit tests for MMC3 PRG banking (`tests/unit/test_mappers.cpp`): mode 0 and mode 1 windows, register select from `$8000`, 6-bit PRG mask | Fail on the pre-fix code (4/4), pass on the fixed code | Guards against regressions of M1-M3 |
| M6 | Unit tests for MMC3 scanline IRQ: countdown after latch (N+1 clocks), disable/acknowledge via `$E000`, re-enable via `$E001`, zero-latch behavior | Pass on current code (4 tests). Written as regression guards, not as a fix | Test expectations were corrected to the implemented rules: the `$C001` reload clock does not assert, and `$E000` disables as well as acknowledges |
| M7 | MMC3 IRQ on the mmc3_irq_tests ROMs (screen results read via new `expect-text`): 1.Clocking, 2.Details, 3.A12_clocking PASS (in the gate). 4.Scanline_timing FAILED #3 (scanline 0 IRQ too late). 5.MMC3_rev_A FAILED #2. 6.MMC3_rev_B FAILED #2 | Open | Mega Man 3 is revision B per the readme |
| M8 | Revision B counter rule (assert after any zero result when enabled; reload when cleared or zero) | Test 6 still FAILED #2; zero-latch unit test changes. Reverted | Counter logic is not the cause of 5/6 #2. Suspect A12 delivery timing from the PPU (same class as 4 #3) |

## Fixed (for reference)

| Test | Fix | Commit state |
|------|-----|--------------|
| 7-dmc_basics #19 | `$4015` start fills empty DMC buffer at once (`dmcFetchByte`); restart sets `bitsRemaining = 8` | passes, promoted to tier1 |
| 8-dmc_rates #3 | DMC timer reload `rate/2 - 1` | passes, promoted to tier1 |
| 1-len_ctr #3 | 5-step mode checkpoint bound `< 5` (was `< 4`, so sequencer stalled) | passes, promoted to tier1 |

## 5-len_timing: PPU-style status lag tried (2026-10-21)

| # | Change | Result |
|---|--------|--------|
| L10 | `$4015` length bits observe a lagged copy of the counters (write-side synced) | 1-step lag: test 2 "too soon". 2-step lag: test 3 "too late". Reverted |

Proven: the visibility window between tests 2 and 3 is 1 CPU cycle, and every APU
observable updates on the 2-cycle APU grid. No integer lag lands inside the window.
The PPU analogue worked because PPU dots are 1/3 CPU cycle. Fixing 5-len requires
the sequencer scheduled in master ticks (L9) plus the frame-IRQ recalibration that
L5/L6 showed is entangled with it. That is the remaining scoped task.

## MMC3 revision selection via NES 2.0 submapper (2026-10-22)

nesdev wiki (MMC3, IRQ Specifics) confirms the revision behaviors are mutually
exclusive hardware variants and are encoded as mapper 4 submapper 0 (rev B
"normal": IRQ while counter == 0, so latch 0 IRQs every scanline) vs submapper 4
(rev A "alternate": IRQ only on a change to zero). Implemented:
- `CartridgeInfo::submapper` parsed from NES 2.0 byte 13 low nibble (0 for iNES 1.0,
  so plain headers keep rev B — the majority behavior).
- `Mmc3` picks the rule from the header; `$C001` now also ORs the counter above 127
  (Sharp behavior, per the wiki) so a manual reload counts as a change to zero.
- Playtest `force-submapper <n>` promotes a header to NES 2.0 in memory for testing.
  `5.MMC3_rev_A` passes under `force-submapper 4` and is gated; all six blargg
  MMC3 IRQ ROMs now pass. Unit tests cover both revision rules.

## RESOLVED: T069 MMC3 scanline IRQ (2026-10-22)

| # | Change | Result |
|---|--------|--------|
| M9 | Revision B counter rule (from M8): reload-or-decrement, then assert whenever the counter is 0 with IRQ enabled — including the $C001 reload clock with latch 0 | `4.Scanline_timing` and `6.MMC3_rev_B` PASS; 1/2/3 stay green; gated as playtest entries |

Notes: the reload value in test 4 is the scanline count, and the first counter clock
is the pre-render sprite-fetch A12 rise ("blargg scanline 0"), so reload 0 must assert
on that very clock — the old rule waited one extra clock = one scanline late ("too
late"). M8's earlier "test 6 still failed" result was read from a stale probe binary;
with a freshly linked probe both ROMs pass. `5.MMC3_rev_A` fails by design: the core
targets revision B (Mega Man 3 / SMB3); the two revision tests are mutually exclusive.
The zero-latch unit test was updated to the revision B rule.

## RESOLVED: 5-len_timing — ALL known failures fixed (2026-10-21)

Final model (completes L11):
- Clock events (quarter/half) run from `m_seqClockBase` = write + 3 CPU cycles
  (the hardware restart latency); later periods advance by the plain period.
- The IRQ event runs from `m_seqBaseMaster` = the write itself (no latency) and its
  flag set quantizes up to the APU cycle grid.
- The period wrap is its own event at the period end, AFTER the IRQ event. Firing
  the wrap inside the 29829 clock lost the period's IRQ for a free-running
  sequencer (power-on, no write) — caught by unit `FrameIrqFiresInFourStepMode...`.
- The 5-step period is 37281.5 CPU cycles (the half cycle breaks a sample-time tie
  in period 2, 5-len tests 12/13).

Result: 5-len_timing passes all 13 sub-tests; `KNOWN_FAILING_ROMS` is empty;
135/135 ctest; playtest 11/11; determinism clean on 5 commercial ROMs; nestest
golden log 8991/8991.

## Frame sequencer redesign landed (2026-10-21, no regressions)

| # | Change | Result |
|---|--------|--------|
| L11 | Frame sequencer scheduled at exact master times from the `$4017` write (`m_seqBaseMaster`); step-4 split into half-frame at 29829 and IRQ set at 29830 (separate events); IRQ flag time quantized up to the APU cycle grid | `3-irq_flag`, `4-jitter`, `6-irq_flag_timing` all pass; full gate 134/135 with only `5-len_timing` failing; playtest and determinism clean. Old per-step counting removed |

This replaces the reverted L9 attempt: the IRQ calibration that blocked it is now
the grid quantization of the flag set (no per-test offsets).

### Remaining: 5-len_timing (measured, not guessed)

Both timing references use the same write phase (master mod 24 = 18, verified by trace).
- First length clock must be visible in `(write+14915cyc+6t, write+14916cyc+6t]`
  -> sequencer base at `write + ~3.5 CPU cycles` (the documented 3-4 cycle write latency).
- Frame-IRQ flag must be visible in `(write+29830cyc+6t, write+29832cyc+6t]`
  -> base at `write + <= 2 cycles`.

A single shared base cannot satisfy both (interval is empty); proven by sweeps
LAT {0..6} x VIS {0..36} and by the floor/ceil grid analysis. The model that
remains to be built: the write latency applies to when clock *events* resume, while
the IRQ flag stays bound to the APU grid relative to the write itself. Tests 2-11 of
5-len already pass under the LAT=3 variant (before the flag-grid change), so the
clock-event side of that model is confirmed.

## Resolution: 03-vbl_clear_time FIXED (2026-10-21)

| # | Change | Result |
|---|--------|--------|
| P7 | `$2002` bit 7 reads the previous dot's status (`m_statusLag`); NMI keeps the live flag; clear dot stays 1 | All 10 ppu_vbl_nmi ROMs pass; promoted to tier 1; gate 134/135 |

The W sweep (write offset 0..12 x clear dot 1/2) proved no bus-offset pair satisfies
03 and 07 together, which pointed at the read path itself lagging the live level.

## Offline sweep (timing model step 2)

| Sweep | Result |
|-------|--------|
| CPU sample offset S, 0..12 ticks, full set | Only S=6/7 keep all non-known tests green; no S fixes 03 |
| S x pre-render clear dot (0-3), PPU/NMI set + 4-jitter + 6-irq | 03 passes at clear dot 2 for all S; 07 passes at clear dot 1 for all S; no cell passes 03, 07, 10 and 6-irq together |

Conclusion: the 03/07 conflict is over the clear dot, not the CPU sample offset.

## Dead ends

- DMC timer without `/2` (`kDmcRates` used as-is): 8-dmc_rates fails rate 0.
- DMC timer with `/2` and no `-1`: 8-dmc_rates fails rate 0 (period 1 APU cycle too long).

## T037 WIP findings (2026-10-22, branch feat/t037-dma-alignment)

Landed on the branch (gate 139/139 green):
- OAM DMA rewritten as a cycle-accurate get/put state machine (nesdev wiki DMA):
  DMC get preempts an OAM get; dummy/align overlap the OAM stream; OAM realigns
  after (+2 common case, tail cases fall out of the loop).
- Cpu::rd DMC stall is phase-aware: halt + dummy + optional align + get = 3/4
  cycles (was a fixed 3 ticks + read).
- The $4015 load fetch is no longer instantaneous: the load DMA is scheduled
  +3 CPU cycles after the write (wiki: halt on a get cycle during the 2nd APU
  cycle after the write). dmcFetchPending() is now due-time aware; serialized.
  7-dmc_basics/8-dmc_rates semantics still pass (+4 cycles broke #19; +3 fits).

Remaining (the actual sprdma blocker), from blargg's source (koute/pinky mirror):
the ROM times code with a DMC TIMER — sync_dmc syncs to the DMC output cycle,
end_dmc_timer measures elapsed mod 3424 by polling $4015 bit 4 after sample
restarts. So the pass/fail depends on the bit-4 clear latency after a $4015
restart (load-DMA halt+DMA cycles) at cycle accuracy, at rates $0F (432c/byte)
and $00 (3424c/byte), across 16 alignments. Next step: model the bit-4 clear
time as [write + halt latency + DMA duration] and iterate against the row table
(currently still 3768/3767 alternating, i.e. the DMC contribution is constant);
expected CRC $FBADA48D (screen CRC line is the ROM's own computed value).

## T037 session 2 (2026-10-22, branch feat/t037-dma-alignment)

Root cause chain for the sprdma hang, now understood and fixed:
1. The ROM's sync_dmc fine loop NEVER exited: it iterates every 433 cycles
   (429 base + the DMC reload DMA's usual 4 stolen clocks — blargg's source
   comments spell this out), drifting 1 cycle/iter against the 432-cycle byte
   period until the phase aligns. Our loop ran at exactly 432 (no drift): two
   bugs cancelled into a hang:
   a. the DMC stall was 1 cycle short (the halt cycle was never charged);
   b. fetches were serviced at any cycle parity, so the reload DMA's
      alignment cycle never appeared.
2. Fixes landed: halt cycle charged (stall = halt+dummy+align+get = 3/4);
   halt parity scheduled per DMA type (reload halts on PUT, load on GET —
   nesdev wiki DMA), enforced in dmcFetchPending and serialized.
   Result: sync exits, all 16 rows print, gate 139/139 green.
3. Remaining calibration: the rows still show no DMC/OAM collision (flat
   3770/3769, per-row = OAM parity only; 0 preemptions in 16 DMAs). The test's
   delay puts a byte boundary inside the OAM window; whether it lands there
   depends on the DMC timer phase our sync EXIT leaves behind. Sensitivity
   proven: load-DMA due +4 makes rows vary (3093s appear), +5 hangs, +3 is
   stable-but-flat (and +4 breaks 7-dmc_basics #19). Next: model the sync-exit
   phase exactly (the final iteration's load-DMA completion vs the `bit $4015`
   read at write+7..9), then the OAM collision cost (+2 common per wiki).
   Current computed CRC 8D018086, target FBADA48D.

## T037 session 3 (2026-10-22): collisions firing

- Bug found by measurement: the halt-parity gate made dmcFetchPending() false
  exactly at the get cycles where the OAM engine checks it. Added
  Tickable::dmcGetWanted() (same conditions minus the parity gate) for the OAM
  steal; rd() keeps the gated pending.
- Result: the table now varies — measured boundary offsets show the byte
  boundary at DMA+375..381 for ~10 rows (collision, -2 via the overlap
  discount) and DMA+3412..3424 for the rest (clean). CRC F4E1BF7B (target
  FBADA48D). Gate 139/139.
- Disch's forum rules (t=14120, retrieved via search) confirm the model and add
  the write-slip nuance: a put-parity halt landing on a CPU write slips to the
  next get cycle and the DMA costs 3, not 4 (RMW double-writes: 4 if it lands
  on the 2nd write). Also: a DMC halt succeeds on any OAM DMA cycle, and a
  dummy/align/OAM cycle must sit between halt and get.
- Remaining calibration (next session): (1) the sync-exit phase — cycle-exact
  accounting of the @sync exit iteration's load DMA (halt-on-get at +3/+4,
  halt+dummy+get) versus the `bit $4015` read at write+7..9; a 1-2 cycle
  difference rotates the collision set and shifts the boundary cluster
  (currently mid-DMA at ~378; hardware likely exercises the DMA-tail +1/+3
  end cases on the transition rows); (2) the write-slip 3-vs-4 rule for
  clean rows; (3) the tail cases in the OAM engine for boundaries near
  cycle 513.

## T037 session 4 (2026-10-22): exact expected-value recovery machinery

Goal: recover hardware's 16-row table without a reference emulator, by inverting
the ROM's own check (expected CRC $FBADA48D, embedded in the ROM at $EE13).

Validated pipeline (every step cross-checked against the ROM itself):
- update_crc at $E78A (ON path via pha at $E78E), reset_crc at $E77B (seed
  $FFFFFFFF). The shipped ROM differs from the pinky source: lsr/bcc (standard
  reflected CRC-32, poly $EDB88320, no final XOR) instead of ror/bcs, and
  print_crc ($E7BF) INVERTS each checksum byte before printing — the on-screen
  "CRC" line is the complement of the compared value (our compare state
  $0B1E4084 prints as F4E1BF7B).
- Stream structure (4195 bytes at compare): 19-byte title + 16 x [label byte
  $00-$0F, $8F, $FFx... 255x $83, 4 digit chars]. Verified: per-event zp state
  matches zlib.crc32(stream)^$FFFFFFFF exactly at every one of 4196 events.
- sprdma_and_dmc_dma_512: expected $F1A58F55; our table there ALREADY shows
  non-monotonic tail structure (rows 8-14: -2 then +2), proving the DMA-tail
  sub-cases are live in our engine but calibrated differently from hardware.

Searches run (all against the validated pipeline):
1. Contiguous collision run, deltas {0..-4}, base 3600-3850, 2 phases,
   edge specials: 6.9M hypotheses — no match (machinery verified by finding
   our own table).
2. 3-segment step function, deltas {0..-4}: 9.6M — no match.
3. (running) DFS with affine CRC composition: base 3650-3850, parity modes
   {alternating, none}, up to 3 segments with per-segment deltas {-3..+3}
   (captures the ±2 tail structure seen in the _512 table).

## T037 session 5 (2026-10-22): search machinery corrected; magnitude question

Two bugs found and fixed in the hypothesis search (the failures were ours, not
the model's):
1. aff_bytes composed byte steps in reverse order (old∘step instead of
   step∘old) — single-byte tests passed because identity commutes.
2. The parity term only modeled odd-high tables; real tables alternate with
   even rows high (odd = base−1). Three parity modes now.
After the fixes the machinery is fully validated: the chain reproduces our
known _512 compare state (0x5064D644) byte-exactly, and the self-search
recovers our own _512 table.

Searches against the ROM constants (all with validated machinery):
- 3 segments, base 3650-3850: 22.16M — no match (either ROM).
- 4 segments, base 3650-3850: 681M — no match (main ROM).
- (running) 3 segments, base 2400-4200: the magnitude assumption. The
  end_dmc_timer primitive (coarse 16-cycle loop + fine 3423-cycle loop, mod
  3424) need not produce values near our ~3767; hardware's base may sit
  anywhere in the mod-3424 fold.
Next escalations if empty: 4 segments wide-base, then free-delta
meet-in-the-middle (affine inversion join, ~43M×43M per (base,mode)).

## T037 BREAKTHROUGH (2026-10-22): reference tables recovered; the FBADA48D red herring

Method (approved by the project owner; GPL compatibility analysis on record):
RustyNES (GPL-3.0-or-later, passes both ROMs per its committed oracle) was used
purely as a MEASUREMENT ORACLE — a 30-line bin built against its public API ran
the vendored blargg ROMs and dumped nametable text + the update_crc byte stream.
No RustyNES code enters this repository; values are facts. Rust toolchain:
user-level rustup.

REFERENCE TABLES (RustyNES, "Passed" on both):
- sprdma_and_dmc_dma:      527 528 527 528 527 526 525 526 525 526 525 526 525 526 525 526
- sprdma_and_dmc_dma_512:  525 526 525 526 524 525 526 527 527 528 526 527 527 528 527 528

THE RED HERRING: the shipped ROM's check constant is 0x04525B72, NOT $FBADA48D.
The full-run capture shows check_crc_ comparing zp = 04525B72 and PASSING;
$FBADA48D sits at $EE13 but is not this build's compared value (the pinky
source is a different build — its CRC routine also differs: ror/bcs vs the
ROM's lsr/bcc). Every structured search (1.1B+ hypotheses) targeted the wrong
constant; with 0x04525B72 the reference table's structure (base 525, 2
segments, deltas +2/0, odd-high parity) is squarely inside the 3-digit search
space and would have been found immediately.

Stream facts (from the reference capture, cross-validated: the 4179-byte
segment CRCs to 04525B72 and the ROM passes):
- 16 iterations x 260 bytes: [label $00-$0F][0x8F][0x83 x255][3 digit chars]
- Title 19 bytes; 3-digit values (reference magnitude ~525).

THE REAL GAP: our engine measures ~3767 vs reference ~525. The value is
256*coarse + fine; ours has coarse count ~12 iterations (192 cycles) different
from the reference — our sync_dmc exit leaves the DMC timer phase ~190 cycles
away from hardware's. This is a sync-exit phase issue (which iteration of the
433-cycle fine loop exits), not a 1-2 cycle cost error. Next session: capture
RustyNES's sync-exit iteration boundary (its $4015-write timing inside @sync)
and align our load-DMA completion so the exit iteration matches; then verify
row 0 = 527 and the drift reproduces rows 1-15.

## T037 session 6 (2026-10-22): sync-exit divergence root-caused

Side-by-side write traces (sta $4015/$4014 with cycle stamps; reference via
sprdma_writes bin, ours via instruction trace at the same ROM sites):
- The post-sync delay calibration matches EXACTLY: [last $4015=$10 pair ->
  $4014] gap = 3006 cycles in BOTH engines. The test machinery after sync is
  aligned.
- The reference's fine @sync loop runs ~7 iterations (writes 433 cycles
  apart: e.g. 1228888..1231053) before exiting; OURS writes no $1F loop
  writes at all near the DMA — our sync exits immediately.
- Mechanism: each @sync iteration restarts a 1-byte sample and reads $4015
  bit 4 ~7-9 cycles later. The loop continues while rem>0 (fetch not yet
  done). Hardware's load DMA completes at ~write+10 — just PAST the read —
  so the loop continues until 1-cycle/iteration drift (433 vs 432) walks the
  completion under the read boundary (~7 iterations). Ours completes at
  ~write+6-7 — BEFORE the read — so bit 4 reads clear on iteration one.
- Consequence: our sync exits at a DMC-timer phase ~190 cycles away from
  hardware's, which propagates to the coarse counter (~12 iterations = 192
  cycles) and produces our ~3767 table vs the reference ~525.

Next fix (precisely scoped): the load-DMA halt scheduling must include the
write-cycle retry (Disch: a halt attempt landing on a CPU write cycle is
delayed, not stolen; the sta $4015 write itself occupies the cycles where the
halt would land) so that in this alignment the fetch completes ~3 cycles
later (~write+10). Constraint to preserve: 7-dmc_basics #19 (whose read
lands in a different alignment and passes with the current +3 due — the
alignment dependence is exactly the write-retry effect).

## T037 session 7 (2026-10-23): corrected traces; divergence chain quantified

CORRECTION to session 6: its "ours exits the fine loop immediately" conclusion
was an artifact — both tracers used a WRONG site list (an earlier regex matched
lda#imm+sta patterns at the match start, 3 bytes before the real sta). Real
sta $4015 sites: E20B,E210,E214,E234,E282,E286,E29A,E36E,E373,E8B5,E8DC,EB14,
EB3E; sta $4014 at E500. With correct sites both engines run IDENTICAL
machinery per iteration:
  fast pair ($1F x2 at E210/E214) -> fine @sync loop (E234, 433-cycle spacing)
  -> exit -> test_ $10 pair (E36E/E373) -> $4014 OAM DMA -> end_dmc_timer
  restarts (E282/E286) -> coarse/fine loop (E29A).
Invariants that match EXACTLY: exit->$10-pair gap 53, $10-pair->$4014 gap 3006.

The divergence chain (first iteration, reference vs ours):
1. fast-sync poll exits 266 cycles later in ours (fine loop entry +821 vs
   +1087 after the fast pair);
2. fine loop runs 14 iterations vs 6 (exit phase ~8 cycles apart);
3. compounds to ~12 coarse-counter iterations (~192 cycles) -> table ~3767
   vs reference ~525.
Calibration targets, in order: the fast-sync poll exit (bit4-clear timing
after the double-restart), then the fine-loop exit count. Tuning knob: the
load-DMA halt scheduling in these contexts (halt-parity/write-retry), with
7-dmc_basics #19 as the constraint.

Session 7 addendum — fast-sync poll mechanism (why 266 cycles):
Both engines' sync_dmc_fast polls run ~100+ iterations (~800 cycles), not
1-2: after the double $1F restart, rem>0 while the fetch waits for the sample
buffer to empty — the output unit must first consume the buffered byte (up to
432 cycles at rate $0F). The poll duration therefore measures the DMC timer
phase itself, i.e. the fast-sync exit difference (266 cycles) IS a DMC timer
phase difference inherited from before the loop (boot + $4010 rate-switch
timing + prior state), not a load-DMA cost error. Calibration must therefore
start from the first DMC divergence between the engines — diff the
bit4-clear timing after the very first $4015 enable at boot, then walk
forward iteration by iteration. Tools are in place: /tmp/our_writes (pc-site
tracer, correct sites), /tmp/rustynes target/release/sprdma_writes (same for
the reference), /tmp/flow_ours (instruction-stream window dump).

## T037 session 8 (2026-10-23): bit-4 timelines — first divergence localized

Tooling: sprdma_bit4 bin (reference; `bit $4015` sites + post-instruction Z
flag) and a temporary statusRead hook (ours; removed after capture). Both
timelines record (cycle, bit4) for every $4015 poll; runs separated by >20c.

Result (offset-corrected, ours=ref+5 from boot):
- Run 0 = sync_dmc_fast poll: reference exits (bit4 clear) after 57 polls
  (1228476); ours after 95 (1228747) — 266 cycles longer, 38 extra polls.
- Runs 1-5 (fine @sync): identical decisions (set), delta constant 266.
- Run 6 = the fine-loop exit race: reference reads CLEAR (exits); ours reads
  SET (continues) — ours takes 14 iterations total vs 6.
- Run 12+ (coarse): the 266-cycle phase error compounds to ~13.8k cycles.

Conclusion: the ONLY upstream divergence is the DMC timer phase at sync
entry. No DMC activity occurs during boot (no bit4 reads between the boot
enable at ~103770 and 1228080), so the free-running DMC output-unit timer
accumulated a 266-cycle (532 APU-cycle) phase difference silently since
power-on. Next: compare the DMC timer counter value in both engines at the
$4010=$0F rate write (~1228055) — candidates: whether the timer free-runs
while idle (ours: yes, clockDmcTimer is unconditional), the rate-write
effect on the current counter, and APU-cycle accounting during the boot
delay's DMC stalls (the boot $4015=$00 at ~103770 clears rem but the DMC was
never started — no stalls — so the phase difference must originate in the
idle-timer model itself).

## T037 session 9 (2026-10-23): load-DMA entry model corrected; reload chain isolated

Landed (all tests green, 139/139):
- Load DMAs no longer parity-gated: a load refused by the triggering write
  enters on the next READ cycle whichever half it is (wiki DMA / Disch
  t=14120). Reloads keep the put-parity halt gate.
- Load DMAs skip the alignment cycle (span 3: halt, dummy, get); reloads
  keep span 4. Exposed via Tickable::dmcHaltOnPutForAlign().
- Table shifted one row (collision boundary moved) — the fixes are real but
  the decisive poll (run 6) still reads SET in ours vs CLEAR in reference.

Remaining gap, now isolated to the RELOAD chain: after the output unit's
byte-boundary wrap (bufferEmpty), ours takes ~8 cycles longer than the
reference to bring rem to 0 (fine-loop exit 14 vs 6 iterations = 8 cycles of
drift). Candidates, in order:
1. Our reload service can only start at an rd() of the poll (up to 7 cycles
   after the wrap; put-parity gate can add up to 14). The reference halts on
   the scheduled put CYCLE (cycle-level, not read-level, entry for the halt
   attempt — read-level only for the write-refusal case).
2. Their reload arm has a +1-cycle promotion (pending_dmc_dma_next) and a
   "cannot run within 2 cycles of a previous GET" exclusion — ours has
   neither; net length differences of a few cycles each.
3. The 266-cycle initial fast-sync phase (run 0) is absorbed by the sync and
   NOT the blocker; run 6 is.

## T037 session 10 (2026-10-23): sync internals fully decoded (both engines)

New tooling: sprdma_dmcstate (reference DMC state machine via their public
diagnostics: consume edges, buffer state, polls, restarts).

DECODED MECHANISMS (event-verified both engines):
- Fast-sync exit = the first output-unit CONSUME (byte-timer wrap) after the
  double $1F restart: the wrap's reload clears rem, poll reads CLEAR. Ref's
  first consume at 1228476, ours 1228740 — the 264-cycle power-on DMC timer
  phase difference, now measured at the state-machine level.
- Fine loop: restarts are IGNORED (rem=1 at the writes); each iteration the
  poll (restart+10) reads SET while the consume sits past restart+14; the
  consume drifts -1/iteration (433 vs 432) until consume+chain <= poll ->
  exit. Ref: entry +19, exits iteration 6 at the razor edge (consume exactly
  at the poll cycle). Ours: chain = consume+3 (deliver), enters at -44, so
  must wrap the full 432 period (~390 extra iterations) before exiting.
- Our reload chain measured: wrap -> rem=0 in a constant 3 cycles (matches).

REMAINING QUESTION (sharpened): the fine-loop exit STATE is engine-independent
(same wrap-relative window), so the sync should normalize the initial phase —
yet the tables differ by ~3242 (= 3424-182), implying a ~182-cycle phase
difference survives into the measurement (end_dmc_timer). Next: mechanical
event-by-event alignment of ONE full test iteration (sync exit -> $4010 rate
switch -> delay -> OAM DMA -> end_dmc_timer coarse/fine) between the engines;
the divergence must be in that stretch, not the sync loops.

## T037 session 11 (2026-10-23): regression caught and reverted; accounting insight

Session 9's two experiments were re-examined after the sprdma ROM was found
INFINITE-LOOPING (fine sync locked at exactly 432-cycle spacing — restart and
consume both 432, drift dead, stuck at consume-rel -44 forever; no $4015=$10
in 300 frames):
- REVERTED: load-align removal. Empirically the per-iteration DMC stall must
  total 4 cycles for the fine loop to run at 433 (hardware's period, per
  blargg's source comment). With our rd()-based DMA accounting (halt+dummy+
  [align]+get ticks + the rawRead cycle), loads WITH the align tick produce
  the hardware loop period; Disch's span-3 loads with our accounting produce
  432 and deadlock. RECONCILIATION: our tick-based service double-counts
  relative to a per-cycle engine; a true span-3 load needs the unified
  per-cycle DMA engine (T037's design), not a tick-count patch.
- RESTORED: the reload halt parity gate (reloads halt on put). Removing it
  (experiment A) also fed the lock.
- KEPT: loads service on any read cycle (no parity gate) — doc-supported.

State: sprdma completes again (CRC 6698DC67), all 139 tests green. Full
iteration-1 event timelines captured for both engines (sync exit, consumes,
$10 pair, $4014, end_dmc_timer) — the alignment corpus for the final push,
which is the per-cycle DMA engine where halt/dummy/align/get are charged as
real bus cycles (matching both Disch's spans and the 433-cycle loop).

## T037 session 12 (2026-10-24): measurement stretch matched; coarse count is the gap

Full first-iteration event diff (both engines, all events): the measurement
stretch MATCHES — pair->$4014 = 3006 in both; pre-DMA consume at pair+3000
(ref) vs +2994 (ours) — a 6-cycle DMC timer phase difference from the
fine-loop exit state (+14 ref vs +3 ours); the reload DMA accounting absorbs
the rest identically. Our fine loop took ~6 more iterations (entry phase),
exit states equivalent within ~11 cycles.

Calibration experiment: a timer phase bump at the $4010 rate write sweeps
the first-row value over ~3743..4069 (2 CPU cycles per APU cycle of bump)
but can NEVER reach the reference magnitude ~527: ref row0 = 256*2+15
(coarse X=2), ours 256*14+167 (X=14). The ~12-iteration coarse-count
difference is STRUCTURAL in end_dmc_timer's coarse loop (16-cycle polls of
the 1-byte sample's end after the END pair), not a phase offset. Next:
compare the END-pair -> sample-end -> coarse-exit event sequence between
the engines (our END pair lands post-consume +955 vs ref +948 — start
there); suspects: 1-byte sample end semantics (last-byte rem->0 timing)
and the coarse poll's read position.

## T037 session 13 (2026-10-24): END-pair mechanism decoded; interval gap found

Both engines' END sequences captured (restart pair, buffer/fetch events,
coarse-loop polls, fine-loop writes):

MECHANISM (decoded, matches the ROM source comment): after the end_dmc_timer
restart pair — first $1F's load fetch fills the buffer (~+7); the second $1F
restarts (rem=1) but its fetch is DEFERRED (buffer full) until the output
unit's next timer-wrap consumes the byte; only then does the fetch deliver
(rem->0) and bit4 reads CLEAR — the coarse loop (16-cycle polls) measures
exactly this interval.

- REF: pair 1238490 -> CLEAR ~1240967 = +2477 (wrap at +2470, fetch +7).
- OURS: pair 1240931 -> CLEAR ~1244250 = +3319. Both engines' coarse loops
  then hand off to the 3423-cycle fine loop.
- ANOMALY in ours: the timer wraps around the pair are at 1239966+3424k =
  1243390, 1246814... yet our consumes print 1244246, 1247670 (856 later,
  then 3424-spaced). One wrap interval stretched by 856 cycles — needs
  explanation before anything else (suspects: our reload fetch service
  during the coarse loop interacting with timer accounting; the FETCHD hook
  gap meant fetches via rd() were invisible in this capture).

Next: log OUR rd()-serviced fetches (dmcDeliver, not dmcFetchByte) around
the pair and reconcile the 856-cycle stretch.

Session 13 correction: with fetches logged via dmcDeliver (the rd()-served
path; dmcFetchByte only covers the $4015-immediate path), the full ours
sequence is:
  CONSUME 1239966 (post-DMA byte taken; silence after)
  END pair 1240921/1240931; DELIV 1240929 (first $1F byte into buffer,
    rem->0 — via the $4015-write IMMEDIATE fetch path)
  CONSUME 1244246, DELIV 1244253 (buffer drained at wrap, second $1F's
    deferred fetch delivers, rem->0 -> CLEAR; coarse exits; fine loop
    writes from 1244272)
REF: pair 1238490 -> drain+fetch at 1240960/67 (+2477). OURS: +3315.
OPEN QUESTION (precise): the byte delivered at 1240929 should be taken at
the next byte boundary on the 3424-grid from 1239966 = 1243390, but the
take printed at 1244246 (+856 = two byte periods later). Either the take
was missed at 1243390 (bits_remaining phase?) or the immediate-fetch path
disturbs the output unit's bookkeeping. Next: trace bits_remaining/timer
around 1240929-1244246; also suspect the $4015-immediate-fetch (which
bypasses DMA accounting) interacting with the buffer/bits state.

## T037 BREAKTHROUGH 2 (2026-10-24): the bits-counter re-anchor — table at reference magnitude

Root cause found by continuous WRAP2 tracing (timer==213 logs): between the
takes at 1240824 (bits=6) and 1241252 (bits=7) the bit counter went UP —
our $4015 restart path set bitsRemaining=8 (the old 7-dmc_basics #19 fix),
re-anchoring the output unit's phase at every restart. The reference's bits
counter is free-running (a restart must NOT reset it; the buffered byte is
taken at whichever wrap follows).

FIX: removed the bitsRemaining=8 reset from the $4015 restart. 7-dmc_basics
still passes without it. RESULT: the table collapsed from ~3767 to the
reference magnitude — ours now 527/529/527/529/525/527/525... vs reference
527/528/527/528/527/526/525/526... (within 1-2 cycles per row; CRC
C174E79B vs the expected 04525B72). The _512 variant similarly lands at
~525-529. Remaining: the ±1-2/row calibration — the fine-loop exit-state
difference (+3 ours vs +14 ref) is exactly this scale.

## T037 session 15 (2026-10-24): exit-geometry quantified — deliver latency is the last knob

Post-bits-fix geometry (both engines, fine-loop exit iteration):
- OURS: restart W; DRAIN (buffer empties) at W+5; DELIV (deferred reload
  fetch, rem->0) at W+10; the poll read at W+13 reads CLEAR -> exit.
  Exit condition: drain+5 <= poll -> drain-rel <= +8.
- REF: restart W; their consume at W+14 with rem already 0 AT the poll cycle
  (their refetch lands ~drain+0/1). Exit drain-rel <= +13.
- Net: our exit state sits ~5-9 cycles earlier than theirs = the observed
  +-1-2/row table residue (ours 527/529/.../525/527 vs ref 527/528/.../526/525).

THE LAST KNOB: our deferred reload after DRAIN delivers at +5 (waits for the
next rd() in the loop code, then halt+dummy+align+get); the reference's
per-cycle engine delivers at ~+1. Matching requires the reload fetch to be
served from the drain instant with proper per-cycle accounting (the unified
per-cycle DMA engine — the same conclusion as session 11's reconciliation,
now at ±1 cycle scale instead of ±433).

## T037 session 16 (2026-10-24): per-cycle DMC DMA engine — HALF the rows exact

Implemented: the DMC DMA now fires from tickHalf at the drain instant
(Cpu::serviceDmcDma: halt, dummy, [align], get as real cycles; re-entrancy
guard m_inDmcDma; the old rd()-based service removed). All 139 tests green.

sprdma tables vs reference:
- main: ours [?]528 528 528 528 528 | 526 526 526 526 526 526 526 526 526 526
        ref 527 528 527 528 527 | 526 525 526 525 526 525 526 525 526 525 526
  rows 1,3 and all tail-odd rows EXACT; rows 0,2,4 and tail-even rows +1.
- _512: similar — flat 526/528 vs ref's alternating 525/526/527/528.
Residue: a pure ROW-PARITY 1-cycle offset — ours is flat where hardware
alternates. Cause candidate: the exit-state quantization now lands at a
fixed drain-rel per row; hardware's alternates (the fine loop's 433-cycle
iteration vs the 432 grid leaves a ±0.5 remainder that flips per row).
Next: inspect our per-row exit drain-rel (expect constant; hardware
alternates between two adjacent values) — likely the halt parity gate
(reloads halt on put only) quantizing our exits onto one phase.

Session 16 addendum: whole-cycle gating of the DMA service start
((now % cpuDivisor)==0) restored the per-row alternation. Main ROM now:
ours 527 529 527 529 527 | 527 525 527 525 527 525 527 525 527 525
  ref 527 528 527 528 527 | 526 525 526 525 526 525 526 525 526 525 526
ALL rows where hardware reads ODD (527/525) are EXACT; rows where hardware
reads EVEN (528/526) are +1 — our alternation swings 2 where hardware
swings 1. Our per-row exit drain-rel was constant +5 (pre-gating); the
remaining asymmetry = the odd-row exit landing 1 cycle late (our deliver
alternation is 9/11 where hardware's is 13/14 — same span, different
phase). Next: align the alternation CENTER (suspect: which parity the
reload-halt gate admits first after the drain — currently the first
whole-cycle boundary; hardware halts on the put cycle specifically).

## T037 session 17 (2026-10-24): alternation localized INSIDE end_dmc_timer

Per-row exit geometry with the whole-cycle gate: EVERY row exits at
drain_rel=+5, deliv_rel=+10, drain parity even — perfectly constant. The
sync normalizes the phase exactly as designed. Therefore the +-1/2 cycle
table alternation (ours 527/529 vs hardware 527/528) originates DOWNSTREAM:
inside end_dmc_timer's own coarse (16-cycle) / fine (3423-cycle) loops,
whose quantization depends on sub-cycle parity at the poll. Our fine loop
resolves alternate rows 2 cycles apart where hardware resolves 1 — suspect
the poll read's sampling of $4015 bit 4 across DMA-halted cycles (the
repeated-read semantics) or the fine loop's own drift arithmetic. The sync
machinery is DONE; the remaining work is one quantization detail in the
measurement loop, not the DMA engine.

Session 17 addendum — the shape is solved:
- Constant 4-cycle DMC DMA span (serviceDmcDma always halt+dummy+align+get;
  the blargg source's fine-sync comment counts a fixed 4-cycle DMA per
  iteration) makes our alternation EXACTLY hardware's: ours
  528 529 528 529 528 | 527 526 527 526... vs ref 527 528 527 528 527 |
  526 525 526 525... — identical pattern, every row +1.
- due=+4 rejected (breaks 7-dmc_basics #19; the whole-cycle gate quantizes
  the due offset away anyway).
REMAINING: one global 1-cycle shift. The exit drain-rel is +5 (ours) vs
+13/+14 (hardware) — a constant, normalized offset that nonetheless lands
the final measured value 1 cycle high. Next: the end_dmc_timer COARSE loop
(ldy #-$45; 16-cycle iterations) — its exit boundary is where the +1
enters; check our coarse iteration length and the poll read position vs
the reference's (their 155-poll run vs ours).

Session 17 final note: the immediate-fetch path was already removed (only
the due-scheduling remains), so the +1 is the load DMA's TOTAL cost from
the $4015 write: our [wait-until-due (1-2) + span (4)] = 5-6 cycles where
blargg's loops count EXACTLY 4 (both his sync loops: 429/3419 code + 4 =
433/3423). The fix: the load DMA must total 4 cycles from the write —
service starts at the write's next whole cycle (due effectively +1) with a
3-cycle span (halt, dummy, get — Disch's load span), reserving the 4-cycle
span (with align) for reloads. Constraint: 7-dmc_basics #19 (the +3 due was
tuned for it; with the whole-cycle gate the effective start is the same
boundary, so retest). This is the last free parameter before both ROMs
match.

## T037 session 18 (2026-10-24): 15/16 rows EXACT

The load DMA now totals 4 cycles from the $4015 write (due at the write;
the whole-cycle gate starts the span at the next boundary; loads span 3
halt/dummy/get, reloads 4 with align — Tickable::dmcIsReloadFetch).
MAIN ROM: rows 1-15 = 527 528 527 528 527 526 525 526 525 526 525 526 525
526 525 — the reference table EXACTLY. Row 0 = 528 vs ref 527 (+1) — the
only mismatch. Row 0 is the first sync after boot (unique entry phase).
_512: mid-table rows differ (different delay constants shift its phase);
same class of residue. Next: row-0 entry phase — the first sync's entry
inherits the power-on DMC phase (the 264-cycle difference measured in
session 10); all later rows re-sync from the previous row's end state.

Session 18 addendum: row 0 also exits the sync at drain_rel=+5 (identical
geometry to rows 1-15) — the +1 enters at the end_dmc_timer COARSE-loop
entry: row 0's coarse exit leaves a different 16-cycle sub-phase (boot
residue: the first sync runs from the power-on DMC state, later rows from
the previous row's end state), and the fine loop's exit lands one iteration
boundary differently. Hardware's ROM is power-on-phase independent (fixed
CRC), so our row-0 +1 is a real residue, not phase luck. Next: compare our
row-0 coarse-loop iteration count with the reference's (~155 polls, session
13 data) — a one-bucket difference at the coarse exit is the last bug.

Session 18 final: row-0 differential pinned. Coarse-loop exits (read of
$4015 returning bit4 clear at the sample end):
- REF: fine exit -> coarse exit = 9913 cycles (155 iterations).
- OURS: 9898 (153 iterations) — 15 cycles earlier.
The 15-cycle entry difference (pair->drain phase, i.e., the boot residue
the coarse loop quantizes) shifts the fine loop's entry by ~1 bucket;
its 3423-vs-3424 resolution then lands X+1 instead of exact. Later rows
re-sync from the previous row's steady state and match exactly (15/15).
Fix direction: the coarse entry phase — our end_dmc_timer restart pair
lands ~15 cycles different relative to the DMC grid on the FIRST test
only; suspects: the begin_dmc_timer rate-switch ($4010=$00 write timing
vs the last drain) and the fast-sync exit residue feeding the first END
pair. This is the last 1 cycle of the last row of the last test.

Session 19 (2026-10-24): row-0 chain traced to the rate switch.
- Instrumented $4010 (the begin_dmc_timer rate switch to $00): ours fires
  31 cycles after the fine exit; the first post-$10-pair consume lands at
  pair+2996 vs the reference's pair+3000 — 4 cycles early on ROW 0 ONLY
  (later rows' consume offsets match: the 3424 grid resyncs each row).
- RATE_RESET (reload the countdown at the rate write) rejected: collapses
  the table and breaks a unit test — carry-over is correct.
- The 4-cycle residue propagates from the fine-sync exit drain-rel
  difference (+5 ours vs +13/14 ref): the grid phase at the switch differs,
  and the first 3424-wrap lands 4 earlier. Because later rows resync from
  steady state, only row 0 carries it. Fix direction: the fine-sync exit
  window — our exit condition admits drain_rel=+5; hardware's admits
  +13/+14. The window edge is the deliver-vs-poll race: with the load DMA
  now totaling 4 (session 18), our deliver lands drain+? vs hardware
  drain+~0 — recheck the exit iteration's deliver position; suspect the
  whole-cycle gate admits the DMA one boundary early in the exit iteration.

Session 19 addendum: exit geometry re-confirmed constant across ALL rows
(drain_rel=+5, deliv=+10) vs hardware (+13/+14, deliver ~+1). The 9-cycle
exit-state difference is engine-wide but benign for rows 1-15 (each row
resyncs from steady state); only row 0 inherits it through the rate-switch
phase (4-cycle consume offset). The endgame knob: our deliver latency
after drain (5 cycles: 1 boundary wait + 4 span) vs hardware ~1 — closing
it shifts the exit window 4-8 cycles later, aligning row 0's rate-switch
phase. Candidate: serve the reload fetch from the drain instant without
the boundary wait (the drain IS a whole-cycle event in the APU clock —
the gate's boundary check may be redundant for reloads and cost the extra
cycle). Verify against rows 1-15 (must stay exact) and 7-dmc_basics.

Session 20 (2026-10-24): reload boundary-bypass tested and reverted — the
deliver latency is the SPAN, not the wait. Deliver remains drain+5 (halt+
dummy+align+get). Hardware's same-cycle refill (their consume trace shows
full_after=1 AT the consume cycle) implies the fetch completes BEFORE the
consume edge exposes it — i.e., the reference model arms and performs the
reload fetch while the last byte is still playing (needs_dma is level-
held, the per-cycle engine opportunistically serves it during the tail of
the byte period, overlapping the sync loop's own cycles). Modeling THAT
is the true row-0 fix: the reload fetch must be servicable BEFORE the
buffer drains (arm on rem>0 + buffer-will-drain, serve during the tail),
not after. That is a semantic change to dmcFetchPending (arm earlier,
serve when the shifter takes the last bit). SCOPED for the next session;
all guardrails in place (rows 1-15 exact must be preserved; 7-dmc_basics;
139 gate).

## T037 session 21 (2026-10-24): tail-arm hypothesis tested and rejected

Model tested: arm the reload fetch at the LAST-bit wrap (one period before
the consume) so the refill is present AT the consume edge — motivated by the
reference's same-cycle full_after=1. RESULT: breaks 7-dmc_basics (#2) and
8-dmc_rates (rate 0 "too short" — each byte plays one period less). First
patch application also silently missed (a git checkout had wiped the edit
site; detected because the table CRC didn't move). CONCLUSION: hardware
arms the reload AT the consume; the 4-cycle DMA span (deliver = drain+4/5)
is correct; the reference's same-cycle refill display is print-timing, not
a +1 deliver. All tail-arm code removed; 139/139 restored; the high-water
mark stands: main ROM 15/16 rows exact, row 0 +1 from the coarse-entry
phase (boot residue chain, sessions 18-20 notes).

The row-0 fix remains open with a narrowed hypothesis space: NOT the
deliver latency (settled this session), NOT the rate-write reset
(session 19) — the remaining candidate is the coarse loop's entry phase
itself: our first begin_dmc_timer's restart pair lands ~15 cycles
different relative to the DMC grid, which the 16-cycle coarse loop
quantizes onto a different bucket. Fresh-eyes comparison of the ENTIRE
boot-to-first-END-pair event chain against the reference (both probe
suites are in place) is the recommended next step.

## T037 session 22 (2026-10-24): boot chain diffed; row-0 residue = boot APU phase

Full boot-to-row-0 event diff (ours vs reference), findings:
- exit->$10-pair gap: +53 cycles in BOTH engines (the code path matches).
- $4010 rate write: both engines carry over the countdown (no reset) —
  semantics verified against the reference source (write_ctrl touches
  timer_period only).
- Power-on DMC state: both initialize timer=0, bits_remaining=0.
- REMAINING: the APU-vs-master-clock phase at boot. Ours: Apu::runTo
  advances to floor(master/(cpuDiv*2)) — the APU's step boundary lags the
  reference's end-of-CPU-cycle tick by up to one APU cycle. This half-cycle
  semantic shifts the whole DMC grid by ~1-2 CPU cycles, changing the first
  fine-sync's iteration count (ours 3 vs ref 6 — the same normalized exit,
  but the subsequent rate-switch lands on a different wrap: pair+2996 vs
  +3000). Later rows resync (exact 15/15); only row 0 carries the boot
  phase.
- Next: align the APU step boundary to end-of-CPU-cycle semantics (advance
  the APU to ceil or restructure runTo's target by one APU cycle) and check
  row 0 + the full gate. This is sub-cycle scheduling work — the same class
  as the frame-IRQ quantization already in the codebase.
Also this session: committed inert session-20 backdate remnant removed
(gate-verified 139/139 before commit).

## T037 session 23 (2026-10-24): row-0 root = ~6-cycle boot-phase divergence

Measurement conventions reconciled (ours prints mid-write, the reference at
instruction start): the real divergence is ~0 cycles at the boot's first
$4015 write and ~+6 by the first $4015=$0F after the 1.1M-cycle boot
preamble. The boot is pure CPU + PPU $2002 vblank polling — a ~6-cycle
accumulation is consistent with a handful of vblank-poll iterations exiting
one cycle differently (each poll iteration is 12 cycles; our PPU VBL read
timing passes every ppu_vbl_nmi test, as does theirs — the difference is
below test-observable resolution but shifts the absolute boot phase).
That phase propagates: DMC grid phase at the first sync -> exit edge
(+5 vs +14) -> rate-switch wrap (pair+2996 vs +3000) -> row 0 = 528 vs 527.
Rows 1-15 resync and are exact.

DECISION POINT (documented for the owner): matching row 0 exactly requires
bit-identical absolute boot timing — chasing single PPU-clock differences
in vblank-poll exits that both engines' test suites consider correct. The
pragmatic close: accept 15/16 + the _512 residue, gate BOTH ROMs as
XFAIL-with-reference-table (documenting our values vs hardware's), and
finish T037's OAM/DMA deliverables (which are complete and spec-correct).
The alternative — a boot-phase-matching campaign — is open-ended.

## T037 session 24 (2026-10-25): the +7 is a REAL PPU bug — user's call validated

The boot-phase "residue" is NOT benign. Bisection: instruction-stream diff
of the [update_crc call 19 -> call 20] window (402k instructions, both
engines) found the FIRST divergence at instruction 22952:
- OURS: the EA48 vblank poll (bit $2002; bmi, 7 cycles/iter) runs ONE MORE
  iteration — exits at 206088; REFERENCE exits at 206081.
- I.e., at this vblank our $2002 flag reads CLEAR one poll longer: our
  vblank flag becomes visible ~1 PPU clock LATER than the reference at
  this alignment. All prior instructions identical.
- Root: the m_statusLag mechanism (installed for 03-vbl_clear_time: reads
  see the PREVIOUS dot's bit 7) delays flag-set visibility by 1 dot; this
  poll's read lands exactly on the set boundary (last read ~206081+4=sample
  at ~206084; the vblank set lands within 1 dot of it). The reference
  resolves this boundary for the SET the other way (visible at the set
  dot, lagged at the clear — or a mid-read race resolution we model
  differently).
- IMPACT (answering the owner's question): any game polling $2002 in a
  loop can see vblank up to 1 PPU clock (~1/3 CPU cycle) later than
  hardware at this alignment -> one extra 7-cycle poll iteration per
  affected frame. Games resync downstream, but mid-frame poll loops
  (splits) shift 7 cycles. REAL accuracy bug; the ppu_vbl_nmi suite
  passes because none of its tests probe the set dot at this exact
  read-phase alignment (03 probes the CLEAR side).
- NEXT: model the set-dot read race explicitly (nesdev PPU_registers:
  reading one PPU clock before the set -> 0 + NMI suppressed; at the set
  -> race). Our statusLag is global; the hardware behavior is asymmetric
  (lag applies at the clear side; the set side has the suppression race).
  The sprdma row-0 +1 is downstream of this single poll iteration.

## T037 session 25 (2026-10-25): the reset-cycle bug — found, fixed, verified

THE ROOT BUG (validating the owner's refusal to accept): our reset sequence
was 7 cycles; the reference's is 8. Proven by instruction-stream diff from
power-on: first divergence at INSTRUCTION 0 (E85F: ours CYC 7, ref CYC 8).
One cycle at reset shifts every absolute CPU-PPU-DMC relationship.

LANDED (all 139 green, verified before commit):
- Reset = 8 cycles (six internal ticks; cpu.cpp).
- PPU power-on lag: Ppu::runTo processes dots through master-12 (the
  hardware reset synchronizer; with fetch@8 the first instruction shows
  PPU (0,21) per nestest.log — the reference's documented PPU_OFFSET model).
- trace.cpp: CYC column renormalized (-1) to the golden-log convention
  (first fetch = 7); nestest 8991/8991.
- Two unit tests' master targets +12 (the documented lag).
- Asymmetric status lag (ppu.cpp): the CLEAR transition still reads back
  set through its dot (03-vbl_clear_time), but a SET earlier in the same
  dot is visible (the boot-poll alignment).
RESULT: the boot instruction streams now match the reference EXACTLY for
59,563 instructions — through the 500ms delay, to the single decisive poll
at cpu 206079. The remaining difference is ONLY the race-window model:
our dot<=2 forced-clear window swallows the reference's set-returning read;
narrowing it to dot<=1 breaks 02/05/06 (the window is load-bearing for the
suppression tests). The reference resolves the at-set race with sub-dot
(master-clock) granularity that our dot-granular model cannot express.
NEXT: sub-dot race window — suppress only when the read's master sample
point is within ~1 PPU clock of the set's master instant (compute from
m_sched.now() vs the set's master time), rather than by dot number.

## T037 session 26 (2026-10-25): sub-dot race window — first attempt failed cleanly

Attempted the master-clock race window (suppress only within ~2 PPU clocks
of the rise). Two implementation traps found, both fixed or reverted:
1. m_sched.now() inside stepDot is the CATCH-UP TARGET master, not the
   dot's own master — a recorded "rise instant" would be wrong whenever
   the PPU catches up in bulk. The correct rise master must be derived
   from the dot counter (dots*4), and the comparison must account for the
   read seeing PPU state through master-12.
2. With the window replaced wholesale, 02/03/05/06 broke (the sync loops
   depend on the current read behavior at more than just the race instant).
REVERTED to the gate-green state (139/139; dot-based race; row 0 = 528).
ALSO MEASURED this session: the reference's global PPU phase is exactly 4
dots ahead of ours at every master (direct sampling); matching it
(masterClock+4) does NOT fix row 0 — their win is the sub-dot race
resolution alone, and their phase conflicts with nestest's golden (0,21)
column, which pins ours. So the global phase is a CONVENTION difference
(both pass nestest + all blargg tests); only the race window is behavioral.
NEXT (cleanly scoped): record the rise master as dots*4 in stepDot;
suppress only when the read's effective sample master (now-12) lies within
[set-4, set+8); re-add the asymmetric-lag branch (it reverted with the
checkout — verify); guardrails: 02/03/05/06 + 139 gate + rows 1-15.

## T037 session 27 (2026-10-25): the reference's read model decoded; two razor edges remain

Rebuilt the $2002 path against the reference's model (read from their source,
bus.rs `cpu_read_register`): VBL latched at READ-START (current status, no lag),
race window = line-241 dot 0 ONLY, suppression via a per-frame latch
(`suppress_vbl_this_frame`), $2002 read drops /NMI unconditionally (their
comment), reads sample the PPU at the CYCLE-START pull with their dot grid
16 master ticks ahead of our old grid (measured, session 26). Their nestest
column therefore differs from ours by convention; our golden comparator now
renormalizes the PPU column -4 dots (as CYC already is -1).

Implemented (all in /tmp/ppu_session27_model.cpp + .hpp, cpu.cpp, machine.cpp,
test_ppu.cpp — NOT committed; working tree reverted to 1bc3a87, gate re-verified
139/139 after revert):
1. PPU phase: runTo processes to master+4 (was -12) — matches the reference's
   (line,dot)-at-master at every sampled point; nestest passes with the -4-dot
   PPU-column renormalization in the two TracePoint fill sites (8991/8991).
2. On-time reads: $2002 returns m_status (m_statusLag one-dot snapshot retired
   from the read path); rise-edge mask + pre-rise side-effect skip (a read
   before the rise must NOT retroactively clear the in-flight set — this was
   eating frame-3 locks in 04); clear-edge restore window [C-8, C+4) for 03.
3. Dot-keyed NMI-kill window (dots 1-4) REMOVED — obsolete under the sub-dot
   race; it was eating legitimate sets (04's missing frame-3 lock).
4. Set visible at now >= R-10 (cycle-start sampling + grid lead); suppress
   window [R-18, R-4) or [R-18, R) depending on test.

RESULTS with this model: 02,03,05,07,08,09,10 + 01 pass at one window edge
(suppress [R-18,R)); 04,06 pass at the other ([R-18,R-4)). THE REMAINING
TENSION: 04 needs the suppress window to END before R; 06 needs it to extend
TO R. This is the reference's two-point intra-read sampling (their "Tier 1.1":
VBL latched at read-START, bits 6/5 masked by read-END transitions) — the next
step is porting that exact split, not another window sweep.

ROW 0 CLARIFICATION (corrects session 26's framing): row 0 is NOT the boot
poll. The screen table is written by the measurement routine (16 iterations of
sync_dmc + timed loop); with the flag model now correct, the table content
still reads 528 527 528 ... = hardware table SHIFTED BY ONE ROW — iteration 0
measures one 7-cycle poll iteration more than hardware. Per the session-6
notes this is the sync_dmc EXIT PHASE (which iteration of the 433-cycle fine
loop exits), i.e. a DMC timer phase issue at the measurement's sync, NOT a
$2002 race. The boot-poll fix was still correct (04 now passes; instruction
streams align with the reference's poll iteration).

Next session, in order:
1. Port the reference's two-point read sampling (resolve 04-vs-06).
2. Full 10/10 singles + 139 gate + golden-hashes/playtest reconciliation
   (frame hashes WILL change: the PPU phase moved — regenerate with cause).
3. Then the sync_dmc exit-iteration alignment for row 0 (DMC timer phase at
   the timed loop's entry; compare our $4015 write cadence inside sync
   against the reference's sprdma_writes capture at the same sites).

## T037 session 28 (2026-10-26): 9/10 vbl singles; the 04 conflict root-caused to the read-cycle interleave

Model rebuilt on the session-27 base (files preserved in /tmp/ppu_session28_9of10.cpp,
ppu_session28.hpp, cpu_session28.cpp; working tree reverted, gate re-verified 139/139):

1. NMI-kill restored WITHOUT the flag clear (the old kill's m_status clear was what
   ate 04's sets; the kill's m_nmiPending cancellation is what 06 needs): 06 PASSED.
   Trap: a python multi-edit died mid-script leaving the arming branch absent while
   the plumbing looked present — verify grep counts, not just presence.
2. Race window corrected to [R-4, R) — the read whose own cycle contains the set's
   completion (02 row 4's "- -" row) — AND the racing read must RETRACT the set
   (m_status bit withdrawn): the set lands during the read's own catch-up, so the
   latch alone is too late. 02 PASSED.
3. Full-gate inventory of the 9/10 model: 136/139 — only 04 (tier1), golden-hashes
   (expected: the PPU phase moved), and playtest differ. nestest passes WITH the
   -4-dot PPU-column renormalization. sprdma table unchanged (row 0 = 528; that is
   the sync_dmc DMC-phase issue, confirmed independent of the $2002 path).

THE 04 CONFLICT (root-caused, empirically pinned):
- 04 test #10's begin->wait_vbl is a FREE-RUNNING poll (no sync recalibration).
  Its poll lands at dm=-2 (dot 2) — inside the race window — and swallows the set,
  delaying the lock exactly one frame; the enable then misses vblank -> 0 NMIs.
- The reference passes because its reads sample at its own cycle-START boundary
  (run_ppu_to called from Cpu::start_cycle AND end_cycle — the access sits BETWEEN
  the pulls; sub_dot 0/2 pre/post). Its poll lands ~8-10 masters past the rise.
- 02/06/08/etc. are IMMUNE to this offset (their syncs recalibrate into whatever
  window the engine has — verified: our screen output matches the reference's
  screen byte-for-byte on 02, including the "T+ 1 2" header which is NOT a failure
  list; the suite's code-00 PASS is the truth).
- Exhaustive sweeps pin our geometry UNIQUELY: PHASE=+4 grid (swept -12..+20),
  visibility edge dm>=0, window [R-4,R), kill dots 1-4, and a uniform sampling
  lead (all anchors shifted together, 0..12) breaks 7 tests at any nonzero value.
  The corpus pins INTERNAL CONSISTENCY, not the absolute anchor; the reference's
  internally-consistent point differs from ours and only 04's uncalibrated poll
  can tell them apart.

NEXT (the real fix, architectural): port the reference's read-cycle interleave —
PPU pulled to the cycle-START boundary before the access and to cycle-end after
(their run_ppu_to/start_cycle/end_cycle structure), NMI level read live by the CPU
(no pre/post sampleNmi in rd()). Then re-pin: PHASE, window, kill dots, clear
edges, nestest renormalization, golden hashes, playtest. After that, T037's row 0
(sync_dmc DMC exit phase) is the last item before the corpus-100% milestone.

## T037 session 29 (2026-10-24 evening): row 0 root-caused to a DMC-stall poll divergence; $2002 ruled out

MAJOR REFRAME (session 29): the committed model already passes ALL TEN vbl singles
(it always did — it is the 139/139 gate). Sessions 27-28's sub-dot rewrite pursued
reference-alignment and peaked at 9/10; it was never ahead of committed on the
corpus. The sprdma row-0 hunt via $2002 was therefore over-weighted; row 0 is not
a $2002 problem:

DECISIVE MEASUREMENTS (traces preserved: /tmp/ours_sprdma_400.txt,
/tmp/ref_sprdma_400.txt; reference instrumented with SETTICK in run_ppu_to):
1. The reference's set completes at its label = OURS (identical grids). Its
   read stamps run +10 at the same instruction (cycle-boundary pulls with the
   access between — verified in their bus source).
2. Frame-by-frame first-visible-poll comparison over 19 common frames:
   - Frames 1-5: ours dm = theirs − 10 EXACTLY — the SAME poll iteration
     returns V in both engines. The +10 is a stamp convention, not behavior.
   - Frame R=17482392: ours firstVis=+42, theirs=+28 — THEIR poll (dm=28)
     sees the flag; OURS (same instruction, dm=18) does NOT — a genuine
     poll-iteration divergence.
   - Later frames: their loop stops polling where ours still polls — the
     measurement loop's poll schedules have DRIFTED — DMC DMA stalls differ.
3. Confirmed the ten singles are EDGE-IMMUNE on the committed model: an
   additive VBL visibility lead (0..16 masters, env sweep) changes NONE of
   their outcomes (02/03/05/06/07/08/09/10 all pass at every lead).
   The absolute edge cannot be pinned by the calibrated corpus at all.
4. A complete access-timing port (sample=now+10, all anchors re-derived
   verbatim: edge dmEff>=0, window [R-8,R-4), kill [R-8,R+4), restore) was
   implemented and tested: 04 PASSES for the first time, but 02/05/06/07/
   08/10 break — our polls sit 10 masters earlier, so the calibrated ROMs'
   decisive reads land outside the ported windows. Without moving the real
   access (scheduler restructure), the port is not landable. Parked.

CONCLUSION: row 0 = the measurement loop's poll PHASE diverges because DMC
DMA stall cycles differ between engines (the loop's lda/bpl cadence is
stretched by stalls; a one-cycle-per-iteration-class difference accumulates
to one full 84-master poll iteration over the run). The blargg DMC suites
pass in both engines — the residual is sub-test granularity.

NEXT (sharply scoped): at the divergent frame (R~17482392), dump both
engines' DMC fetch/halt instants (ours: the existing per-cycle engine's
advance points; theirs: unified_dma_cycle_impl) for the ~10 loop iterations
before the set, and diff the stall counts per poll iteration. One cycle of
stall difference per iteration = the row-0 bug. This is a pure DMC-timing
diff, no $2002 involvement.

## T037 session 31 (2026-10-25): THE ARBITER PARITY FLIP — main ROM 15/16

ROOT CAUSE FOUND AND FIXED: the DMC DMA put/get arbiter phase was INVERTED.
`(now/12)%2==1` (odd cycles = put) delayed every reload DMA exactly ONE
cycle past the read it must stall. The reference (hardware) halts reloads on
the EVEN cycle. Flipping to `%2==0`:

- sprdma_and_dmc_dma: 1/16 -> 15/16 rows exact. Row 0 = 527 (was 528) and
  the whole one-row-shift artifact is gone; rows 6-15 EXACT. Only row 5 is
  off (528 vs 526, +2).
- sprdma_and_dmc_dma_512: 4/16 -> 10/16 (rows 4,5 +1; 6,7 -2; A,B +1).
- Full corpus: 139/139 either way (the corpus never pinned the parity —
  only the hardware-validated sprdma tables do; the reference's first
  reload halt lands on the even cycle, ours now matches).

EVIDENCE CHAIN (all measurements from clean rebuilds — see the traps below):
1. CPU-side event streams (RD4015/W4010 masters) are index-aligned with the
   reference at the constant -10 stamp offset through the boot phase and
   into row 0's measurement (the boot $2002 poll exits of frames 6-7 also
   diverge without the sampling lead, but the ROM's sync machinery absorbs
   them — verified by sweeping a 0..16-master VBL read lead: the table does
   not change).
2. First real divergence: the E219 wait-loop poll (BIT $4015/BNE, 84-master
   cadence) — our reload DMA delivers one bit-period EARLY relative to the
   reference's grid (our first drain 14740992 vs theirs ~14741640 — a
   ~648-master = one rate-15-bit grid offset). A DMCBIAS power-on grid
   shift (swept 0..984) moves our drain onto theirs but does NOT change
   the table (the sync re-derives the phase — the ROM is grid-invariant
   by design; that is why it produces stable values on hardware).
3. The decisive interleave is INSIDE the sync loops: at the coarse loop's
   poll, our reload DMA's halt was parity-delayed to the cycle AFTER the
   poll's read (halt 14772756 vs the read at 14772750), so our poll read
   the PRE-fetch state ($4015 bit4=1 -> loop exits one iteration early)
   while the reference's DMA preempts its read (post-fetch state, bit4=0,
   loop continues). The one-iteration-early coarse exit shifts the whole
   measurement chain -> row 0 +1 -> the one-row-shifted table.
4. The parity flip puts the halt ON the read's cycle -> the read observes
   the post-fetch state -> the chain aligns with hardware. A read-stall
   (RDY) probe added on top of the flip changes nothing further (the flip
   already resolves the interleave).

TWO METHODOLOGY TRAPS (both bit this session AND previous ones — recorded
so they stop burning sessions):
- STALE INCREMENTAL BUILDS: editing a header (apu.hpp) does not reliably
  trigger a rebuild of dependents in this build dir; several mid-session
  "results" were stale-object mixtures. Session 21 hit the same class ("a
  git checkout had wiped the edit site; detected because the table CRC
  didn't move"). RULE: after any header edit, `rm -rf build` (or touch the
  .cpp) and FULLY rebuild before measuring anything.
- OUTPUT-PARSE OFFSET: `sed -n '3,18p' | awk '{print $3}'` on the screen
  dump captures rows 01-0F, whose one-row-shifted values are IDENTICAL to
  the hardware table's rows 00-0E — a shifted table masquerades as a
  perfect one. RULE: always parse the row LABEL ("03|  00 527"), never a
  bare line offset. Sessions 18-29's "15/16 rows EXACT" claims were read
  from label-parsed output, but several of THIS session's probes weren't.

REMAINING (narrowed): main row 5 (+2); 512 rows 4,5 (+1) 6,7 (-2) A,B (+1).
Rows 6-15 of the main ROM re-lock exactly after row 5, so the residue is a
local razor edge in one measurement phase, not a chain shift.

ROW 5 CHARACTERIZED (post-flip, session 31) — DEEPENED (session 32): the
OAM windows are now directly comparable: the reference's DMACYC stream
contains its OAM DMA cycles as long runs, so its windows can be extracted
without re-instrumenting it. THE SPAN TABLE (cycles, ours vs ref):
  rows 0-4: 513/514/513/514/513 = EXACT match
  row 5:    ours 514 (clean) vs ref 516 (one DMC steal) — the +2, exactly
  rows 6-15: 515/516 alternating = EXACT match (our steal machinery works)
So row 5's value is the OAM window itself: hardware's DMC fetch lands
INSIDE the window (a steal), ours fires 54 masters (4.5 cycles) BEFORE the
$4014 and the window runs clean.

MECHANISM (fully quantified): the fine-sync loop (E22B-E23C, 433-cycle
iterations) exits at the first poll that observes the reload DMA's
delivery; the byte-period drift (432 vs 433) walks the boundary under the
poll by 12 masters/iteration, so the exit lands at a RESIDUE R = poll −
boundary (ours 54, the reference's ~48). The print-setup path from the
poll to the $4014 is almost exactly one byte period (3050 cycles = 36600
masters), so the NEXT boundary lands R masters before the $4014 — and the
path is self-referential: if the boundary fires before the $4014 (R above
the threshold), its 4-cycle DMA stalls the print path itself (the $4014
moves later, no steal); if below, the boundary falls inside the OAM window
(the steal). The threshold sits at R ≈ 48-54 — ours 54 (no steal),
hardware's ~48 (steal): a HALF-CYCLE sub-cycle razor decides it. The DMC
get's delivery instant (mid-get-cycle, halt+42) cannot move earlier within
the whole-cycle model, so flipping our exit one walk-step earlier is not
reachable by a simple knob — the next session needs sub-cycle
instrumentation of the delivery-vs-poll instants (and the same class is
likely behind the _512's residues; see below).

_512 STRUCTURE DECODED (session 32): its table is one-row-shifted like the
main ROM was before the arbiter flip — ours[1..6], [9,10], [13..15] equal
the reference's [0..5], [8,9], [12..14] — with local breaks at rows 7-8
and 11-12 (+1 each pair). The flip fixed the main ROM's shift but NOT the
_512's: its first measurement (a 512-byte sample — a different fetch
pattern through the sync) hits a different razor. No reference event trace
exists for the _512 (all /tmp/ref_* captures are the main ROM); analyzing
it needs either a re-capture from the reference (if it can be rebuilt) or
first-principles work from the ROM source.

METHODOLOGY (session 32 addition): a THIRD silent-failure trap — the probe
patch script's str.replace anchors can silently no-op (the DMCBIAS anchor
"m_dmc = {}; if (cold) {" never matched; session 31's "grid lever
disproven" sweep ran with a DEAD KNOB). The knob was fixed and the sweep
re-run: the table is genuinely bias-invariant (0..4400, all 15/16) — the
session-31 conclusion stands, now with valid data. RULE: probe scripts
must assert every replace (the fixed /tmp/probes.patch.py does).

NEXT (sharply scoped): (a) main row 5 — sub-cycle instrumentation of
the reload get's delivery and the E239 poll's sample instants; the target is
the walk-residue threshold (R 54 vs ~48); (b) the _512's first-measurement
machinery (rows 0-6) — same workflow as the main ROM's row 0 but without a
reference trace; (c) then gate both ROMs.

## T037 session 33 (2026-10-25): pre-merge core sweep — five more real bugs

Per the owner's directive (all core bugs resolved before other project
parts), every un-gated suite in the ROM archive was run against the
current core. Results: cpu_dummy_reads PASSES (gating candidate);
cpu_timing_test6, blargg_ppu_tests, dmc_dma_during_read4, read2004,
litewall, full_palette, scanline, read_joy3 = no $6000 protocol (screen
or hash-based verification needed — future gate candidates); and FIVE
real failures, all fixed this session:

1-4. apu_reset family (see the fix commit): $4017 rewritten at reset,
     the reset ORDER (PPU/APU/CPU — the CPU-first order stamped the APU
     sequencer restart 8 cycles late), DMC power-up bitsRemaining=8 (the
     0-init underflowed to 255 and stalled the first sample 256 ticks),
     and warm-reset register-file persistence. All six apu_reset ROMs
     pass; four promoted to tier 1.
5.   oam_stress: OAM attribute bytes (offset 2 mod 4) have no DRAM cells
     for bits 2-4 — mask $E3 at the $2004 read, $2004 write, and OAM DMA
     write boundaries. Passes; promoted to tier 1.

REMAINING from the sweep (T070, new task): cpu_interrupts_v2 rom_singles
2-5 — interrupt hijacking (vector stealing mid-sequence) + the JMP-variant
branch poll delay. 1-cli_latency passes. This is the only known-failing
core suite left; it needs cycle-by-cycle interrupt-sequence modeling.

The sprdma tables were re-verified unchanged after the DMC power-up grid
shift (row 0 = 527/525; the DMCBIAS invariance holds). Gate: 147/147.

## T037 session 32 addendum (continued) — the walk, the straddle, and the one-cycle floor:
the fine loop's full sequence is now traced (restart writes 5196 masters
apart, polls +72 after each write, the boundary walking 12 masters/iteration
under the poll: poll+30, +18, +6, then -6 — the exit fires when the boundary
crosses the poll's own cycle and the DMA stall pushes the read past the
delivery). The exit residue R (poll sample − boundary) is 54 and is FORCED
by the geometry: the previous iteration's poll sits 48 masters before the
delivery, unreachable earlier. Hardware's effective geometry puts its next
boundary ONE CYCLE before its $4014, so its reload DMA STRADDLES the $4014
write cycle — the write completes (writes never stall), the DMA's get lands
inside the OAM window's first cycles, and the window pays the steal (+2).
Ours completes 4.5 cycles before the $4014 (no straddle). The difference
decomposes as: +36 masters of print-path duration accounting (ours includes
the boundary fetch's 48-master stall; the reference's doesn't — its fetch
fell in the window) + ~6-12 masters of stamp-convention fuzz + ONE CYCLE
(12 masters) of genuinely unexplained fixed-path timing (our unstalled
path 36552 vs the reference's 36564). That one cycle is the floor of what
stamp forensics can resolve without the reference's source; the next lever
is either re-building the reference to instrument its print path, or
accepting 15/16 + 10/16 as the gated state (the ROMs still FAIL their
internal CRCs, so they cannot be gated as passing — they would be XFAIL
entries with the tables documented).

NOTE: sessions 30 and 31 ran CONCURRENTLY on the same working tree (two
agents); session 30's reverted probe code briefly collided with session
31's restored probes (the 21:17 "mystery revert"). The E219-loop exit
numbers in both entries independently agree (ours 14741118 vs reference
14741716), cross-validating the traces.

## T037 session 30 (2026-10-24 late): row 0's seed isolated — the DMC transfer-grid phase

Instrumented $4010/$4015/DMC-event traces on both engines (reference: log-2002
feature extended in its bus; ours: env-gated probes, all reverted after
capture). The complete causal chain for row 0:

1. The ROM alternates DMC rate $0F / $00 windows (16+17 writes / 130 frames,
   matching the reference's 31 — the schedules roughly track). Steady-state
   fetch cadence is CORRECT in both engines: 432 cycles/byte at $F (54x8),
   3424 at $0 (428x8) — the rate table and carry-over are right.
2. THE SEED: the FIRST $0F window (write at ~14736.7k masters, same
   instruction in both engines). The ROM then polls $4015 every 7 cycles
   waiting for the DMC bytes-remaining bit to clear:
   - reference loop exit (first $4015=00 read): master 14741716
   - ours: 14741118 — 598 masters (~50 cycles) EARLY.
3. The 50-cycle early exit traces to the DMC transfer-grid phase: the
   buffer-empty -> shift-register transfer (which requests each reload DMA)
   runs on a free-running 8-bit-wrap grid (~864 masters at $F). Measured
   first GET-to-GET spacing after the restart: reference 404 cycles,
   ours 345 — our wrap grid sits ~59 cycles (≈ one 54-cycle bit period)
   earlier relative to the CPU grid.
4. The early loop exit shifts the following rate-window writes; the schedule
   drift compounds through the ROM's measurement phase (poll-schedule drift
   confirmed frame-by-frame: phases match EXACTLY through frame 6, then a
   2-cycle drift by frame ~49), landing the timing-table measurement one
   7-cycle poll iteration off — row 0 = 528 vs 527.

ROOT: the power-on/restart phase of the DMC bit-wrap grid vs the CPU master
grid differs from hardware's by ~one bit period (the same class of absolute-
phase difference as the measured +10 CPU-PPU access offset — tolerated by
every calibrated test, exposed only by sprdma's uncalibrated drain-wait loop).

NEXT (one lever): shift the DMC wrap-grid phase — either the power-on timer
priming or the wrap-instant alignment in clockDmcTimer (the grid is free-
running; a constant master offset at power-on moves every transfer). Target:
the first $F window's loop exit lands at the reference's master (14741716
equivalent), then verify the $4010 write schedule converges (31 vs 33 writes)
and the table's row 0 -> 527. Guardrails: 139 gate (7-dmc_basics/8-dmc_rates
are phase-sensitive!), rows 1-15, _512.

RESOLVED (see session 31, which ran concurrently and landed first): the
wrap-grid lever was tested — a power-on grid shift of 0..984 masters moves
the drain/loop-exit instants onto the reference's but does NOT change the
table (the ROM's sync machinery re-derives the phase; the table is
grid-invariant by design). The early E219 exit measured here is real but
not causal. The causal seed was the put/get ARBITER PARITY (session 31),
now fixed on this branch: main ROM 15/16, _512 10/16, corpus 139/139.

## T070 session 35 (2026-10-26): cpu_interrupts_v2 test 4 passes — the arbiter phase was the whole offset

Root cause of `4-irq_and_dma`'s constant +1 clock: the absolute get/put
arbiter phase, not the $4017 restart parity. The emulator had cycle 0 after
reset as a PUT slot (even put / odd get). Correct (and now baked): the first
half of an APU cycle is a get, so cycle 0 is a GET — even CPU cycles get,
odd put. A DMC *reload* DMA therefore waits for a put (odd) cycle.

Two sites must move together:
- `machine_impl.hpp`: OAM arbiter (`nextIsGet()` = the cycle the next
  `dmaCycle()` will use is even).
- `apu.hpp`: `dmcFetchPending()` reload halt parity (`% 2 == 1`).

Why not the APU parity: the previous session's hypothesis (the sync_apu exit
parity shifts the $4017 restart by one clock) is not falsifiable through the
table — both phase senses give the same test-4 DMA window lengths up to the
last four rows, and the APU flag instants themselves (29830, ceil grid,
visible=0) are pinned by `6-irq_flag_timing`/`4-jitter`.

Evidence:
- 2x2 sweep (OAMPHASE x DMCPHASE): only the JOINT flip yields
  `8 +524, 8 +525, 8 +526, 9 +527` and keeps sprdma's table. Either phase
  alone breaks sprdma rows 0-4 or leaves test 4 at `8,8,9,9`.
- Under the joint flip test 4's full table (including +0..+13) matches the
  ROM's expected hardware table and prints `Passed`.
- Regression: phase 0 vs phase 1 output is IDENTICAL for every
  apu_test/rom_singles, apu_reset, dmc_tests, dmc_dma_during_read4, oam_read,
  sprdma_and_dmc_dma (main + _512) and cpu_interrupts_v2 singles 2/3/5.
  The flip is a global relabel that only test 4 can see.
- Full clean rebuild + ctest: 147/147 (tier1 32, tier2 22, unit 92,
  playtest 24, golden hashes).
- All env probes (`OAMPHASE`/`DMCPHASE`/`W4017`/`GRID12`/`IRQK`/`IRQVIS`/
  `IRQTRC`/`IRQPOLL`/`T070TRC`) removed; `seqIrqEventMaster()` dead code
  removed.

Still failing, unchanged by the flip: test 2 (9/10 rows; last SEC row off by
one), test 3 (hijack rows engaged; NMI grid 1-2 late), test 5 (`test_jmp`
table). sprdma row 5 (528 vs 526) and the _512 rows also unchanged.

## T070 session 36 (2026-10-26): cpu_interrupts_v2 test 5 passes — taken-branch poll point

Root cause of `5-branch_delays_irq/test_branch_taken` row 4 (`04 01 07`
instead of `04 05 0A`): the generic penultimate-cycle latch used the
operand-fetch sample for a 3-cycle taken branch, so a line that rose
between the branch's opcode fetch and its operand fetch was serviced at
the branch boundary. Hardware polls *before* the branch's second cycle and
has no poll before the third cycle, so such a rise is ignored and the next
instruction executes first.

Fix: `sampleIrqCycle()` records the first-cycle sample in
`m_irqSampleFirst`; the end-of-instruction latch uses it when
`m_branchSkipPoll` is set (taken non-page-crossing branch) and keeps the
penultimate sample otherwise. NMI logic is untouched (test 5 has none).

Result: all four subtests (`test_jmp`, `test_branch_not_taken`,
`test_branch_taken_pagecross`, `test_branch_taken`) match the readme,
including the special `04 05 0A` row; the ROM prints `Passed`.
`cpu_interrupts_v2/rom_singles/5-branch_delays_irq.nes` is promoted to
`TIER1_ROMS`. Full ctest 149/149 (tier1 34).

Still failing: tests 2 (`2-nmi_and_brk`) and 3 (`3-nmi_and_irq`).

## T070 session 37 (2026-10-26): cpu_interrupts_v2 tests 2 and 3 pass — sequence polling + hijack point + architectural cycles

Three independent defects remained:

1. **Interrupt sequences do not poll at their own boundary.** After BRK
   (or an IRQ/NMI sequence) the handler's first instruction always
   executes before a newly pending interrupt. Added `m_skipPollOnce`,
   set at the end of `doInterrupt()` and consumed at the next `step()`.
   Fixes `2-nmi_and_brk` rows 8-9 and `3-nmi_and_irq` rows 10-11
   (an NMI edge during the status push / vector fetch is serviced after
   the handler's `sec`, so the pushed flags show C=1).

2. **The hijack vector decision is made after the PCL push, before the
   status push** (nesdev "CPU interrupts": the signal status at that
   point selects the vector). The check previously ran after the status
   push, so an NMI edge during the status-push cycle wrongly hijacked
   BRK. `2-nmi_and_brk` row 8 now follows the BRK vector and services
   the NMI afterwards.

3. **NMI edge/instruction length used bus-access cycles, not
   architectural cycles.** `m_edgeCycle` was `m_cycles` (only rd/wr
   increments) and `m_prevInstrCycles` likewise, so implied 2-cycle
   instructions such as SEC/CLV (one bus access + one `tick()`) had
   length 1 and an edge during SEC's first cycle failed the
   `edgeCycle < prevInstrCycles` penultimate test. Both now use
   `m_instrCycle`. Fixes `3-nmi_and_irq` row 0 (`21` -> `23`).

Result: `2-nmi_and_brk`, `3-nmi_and_irq` and `5-branch_delays_irq` all
print `Passed`; singles 1-5 are promoted to `TIER1_ROMS`. Full ctest
152/152 (tier1 37, tier2 22, unit 92, playtest 24, golden hashes).

Remaining known core issue: T037 `sprdma` main table row 5 (+2) and the
`_512` rows.

## T037 session 38 (2026-10-26): sprdma_and_dmc_dma passes (main and _512)

Both variants print `Passed` with the exact hardware tables:
main `527 528 527 528 527 526 525 526 525 526 525 526 525 526 525 526`,
_512 `525 526 525 526 524 525 526 527 527 528 526 527 527 528 527 528`.

Three defects, found by driving the OAM/DMC overlap from the nesdev wiki
text instead of tuning the tables:

1. **The DMC halt attempt belongs at the start of a CPU cycle, not at
   APU catch-up.** It was serviced inside `tickHalf` as soon as the drain
   was observed during catch-up, which made the fetch start on whatever
   half-cycle the APU happened to be caught up to. `Cpu::serviceDmcIfDue`
   is now called from `rd()`/`wr()`/`tick()` before the cycle's access:
   reads and internal cycles accept the halt, writes refuse it (nesdev
   DMA, "DMA can only halt on CPU read cycles") and the unit retries on
   the next cycle. This is what makes the post-window `_512` reload cost
   3 instead of 4.

2. **OAM DMA is now an explicit cycle machine** implementing "DMC DMA
   during OAM DMA". The old loop serviced the transfer and the DMC fetch
   as separate costs. Now halt/dummy/alignment are no-op cycles that
   *overlap* the OAM stream, the DMC get *steals* an OAM get, and the
   following empty put is the OAM realign. Derived costs match the wiki
   examples exactly: +2 mid-window, +1 for second-to-last put, +3 for the
   last put. This fixed the main ROM's row 5 (the OAM window span) and
   `_512` rows 4-7, 8-9 in one change.

3. **Reload arming vs the refusal path.** A reload is scheduled to halt
   on the first put (odd) cycle at or after the drain, and remains armed
   on every later cycle. A halt refused by a CPU write retries on the
   *next* cycle — which is a get — and that "delayed 1 cycle" reload
   spans 3 (halt/dummy/get). A reload that halted on its scheduled put
   still spans 4 (halt/dummy/align/get), and loads always span 3. Only
   this narrow combination keeps the main table exact AND fixes `_512`
   rows 10/11: the earlier all-cycle retry also changed load spans (a
   load halted on a put cycle gained an alignment cycle), flattening the
   main table to `528 528 528 528 528 526 …`.

Both ROMs are promoted to `TIER1_ROMS`; full ctest 154/154 (tier1 39).

## T071 session 38 (2026-10-26): dma_2007_write passes — all five singles gated

`dma_2007_write` was the last holdout (hardware prints
`22 11 22 AA 44 55 66 77` five times, "no effect"; ours re-issued on
only 1 of 5 iterations). It is the same defect class T037 closed: with
the DMC fetch now attempted at the start of every CPU cycle and accepted
only on a read (a write refuses and retries next cycle), every
iteration's `$2007` write lands on the stall boundary and gets the
re-issue. The ROM's own CRC check now prints `Passed`, and the entry is
gated as a playtest case. All five `dmc_dma_during_read4` singles are
covered.

With T037, T068, T070 and T071 closed, Feature 001 has no open core
accuracy tasks (T059 still needs user-supplied mapper 1/3 titles).

## Session 39 (2026-10-26): the un-gated corpus sweep — six more defect families, five fixed

A sweep of all 208 un-gated corpus ROMs (tier1+playtest covered only 55)
found 8 failing protocol ROMs across 6 root causes, plus 2 more families
exposed while fixing them. Five are fixed; one is documented.

1. **PPU open-bus RC decay** (`ppu_open_bus` — now tier 1). The latch is
   per-bit charge: $2002 reads drive only bits 7-5, palette $2007 reads
   only bits 5-0 (returning decayed bits 7-6), write-only reads charge
   nothing, and everything bleeds to zero in ~0.5 s.
2. **I/O-space open bus** (`cpu_exec_space` both — now tier 1).
   $4000-$4013/$4014/$4018-$401F read the CPU-bus latch instead of 0,
   and RTS/RTI perform a real cycle-2 dummy fetch of PC+1 (observable
   through register side effects when executing from I/O space).
3. **Sprite evaluation was broken three ways** (blargg sprite_hit
   01-11, all now gated and passing):
   - the secondary-OAM clear (dots 1-64) advanced OAMADDR by 32, so the
     scan started at sprite 8 — sprite-0 hit could never fire (01 #2);
   - the step-0 in-range copy did not advance to OAM[n][1], so every
     sprite's tile byte was a copy of its Y;
   - the Y comparison used scanline+1, one line late, cutting each
     sprite's bottom row (03/07/08).
4. **The background shifter reloaded at dot 8k+1 instead of 8k**,
   delaying the entire background by one pixel (02/03/04 left-edge
   rows). Fixed together with the PPUCTRL pattern-table bits in the
   fetch addresses (bit 3 = background, bit 4 = 8x8 sprites — the
   previous code had them swapped; every previously gated test used
   symmetric settings and could not see it). Golden hashes recaptured.
5. **MMC3 revision variants are a selection problem, not an emulation
   bug**: 6-MMC3_alt and 6-MMC6 pass with NES 2.0 submapper 4 (rev A
   semantics) forced; gated as playtest entries.
   `ppu_read_buffer` (bisqwit) went from all-"Hi"-FAIL to all-"Hi"-OK
   from fix 3; its "No-Hi" rows still report mismatches.

REMAINING KNOWN GAPS (documented, un-gated):
- `mmc3_test(2)/4-scanline_timing` #3-#7: the mode-$08 (BG $1000,
  sprites $0000) A12 phase. The wiki itself calls this config
  "partially works" (double-count on the pre-render line); a raw-bus +
  rise/fall-filter model was built and rejected — it fixed #2 but
  regressed the 2005 suite's timing tests. The corpus-calibrated
  synthesized windows are kept; the mode-$08 subtests stay un-gated.
- `sprite_overflow_tests` 2-5: the buggy overflow scan's deeper
  diagonal cases (1.Basics passes and is gated).
- `ppu_read_buffer`: No-Hi rows + result code 41 (78-subtest suite).

METHODOLOGY note: probe binaries must be RELINKED after every core
rebuild — a stale scriptprobe produced one wasted round this session
(the trap is already on record from session 34).

## Session 40 (2026-10-26): T072 — sprite overflow and ppu_read_buffer both pass

**sprite_overflow_tests 2-5** (three overflow-scan defects, all gated now):
1. The per-line reset lived in stepDot's dot-0 block — never reached
   during vblank or the pre-render line, so the overflow state leaked
   across frames into ones that enable rendering late (2.Details #9's
   false positive). The reset now runs in the secondary-clear phase
   (dot 1), where hardware resets it, and also clears m_secSecondaryRead.
2. The scan never stopped when n wrapped past sprite 63: it re-scanned
   from sprite 0 and re-copied already-copied sprites, doubling secCount
   ("8 found" on lines with four sprites). The comparison also always
   uses the primary-OAM byte now (the m_oam2[0] always-in-range source
   was a compensating accident).
3. The buggy diagonal's increment at m==3 is +1 (4n+3 -> 4(n+1)+0),
   not +5 (4.Obscure #5, 3.Timing #11).

**ppu_read_buffer (bisqwit, 78 subtests)**: one defect — m_spriteZeroNext
was never reset per line, so after any line where sprite 0 came in range,
the FIRST in-range sprite of whatever table OAM later held was treated as
sprite 0. Every no-hit DMA half reported a spurious hit. Cleared with the
rest of the evaluation machine at dot 1. Now a tier-1 ROM (code 00).

**4-scanline_timing mode-$08/#10 — new characterization, still open.**
A one-clock bisection (suppress the natural mode-$08 clock, fire one at a
tunable pre-render dot) pins hardware's mode-$08 first clock to pre-render
dot 260 +/- 1 — the SAME dot as mode $10, i.e. the sprite-fetch window
clocks in every table configuration (the garbage nametable fetches
interleaving it are $2xxx-high in all modes). With the window at 260 and
mode-independent, subtests #2-#8 of BOTH suites pass; #9+ (mode $10) then
fail, and moving the mode-$10 window could not recover them because the
background-pulse branch (bit 3 vs bit 4) changes which regions are
continuously high, re-entering the calibrated-guard interaction. The
guarded-edge model needs replacing with a real rise/fall filter whose
thresholds are derived from the fetch pattern itself; left as T072's
remaining item with this bisection on record.

## Session 41 (2026-10-26): T072 CLOSED — fetch-level A12, the last family passes

The session-40 bisection (mode-$08 clock at pre-render dot 260) plus one
fresh look at the reference implementations cracked it:

1. **Mesen's A12Watcher** (fetched from Mesen2): the mapper sees A12 only
   on PATTERN-fetch address changes — a rise clocks the counter if a low
   fetch preceded it by >= 3 master clocks. NT/AT fetches never touch the
   reported bus; CPU $2006/$2007 only while rendering is off.
2. **blargg's own comment** (4-scanline_timing source): `setb
   PPUCTRL,$08 ; sprites use tiles at $1xxx` — PPUCTRL bit 3 is the
   SPRITE table and bit 4 the BACKGROUND table, exactly as the wiki's
   "VPHB SINN" layout numbers them (B = bit 4, S = bit 3) and as Mesen
   decodes them. Session 39's "bit swap correction" was itself wrong —
   invisible to every gated ROM because they set both bits symmetrically;
   reverted here (golden hashes recaptured).
3. With the bits right, the model fell out: mode $08 (sprites $1000)
   clocks at the first sprite pattern fetch (pre-render dot 260); mode
   $10 (bg $1000) at the first background pattern fetch — exactly 256
   dots earlier (blargg's scanline_0_10 = scanline_0_08 - 256), and at
   dot 324 per line steady-state ("cycle 324 of the previous scanline").
   The pre-render line's eight DUMMY sprite fetches (no evaluation runs
   there, secondary OAM is $FF) had to notify too — that was the missing
   scanline-0 clock.

Result: BOTH 4-scanline_timing suites pass (2009's gated as tier 1);
2-details, 1-clocking, 3-A12_clocking, 5-MMC3, and the old
mmc3_irq_tests all still pass. The full corpus sweep now reports ZERO
protocol failures: 186 un-gated ROMs all pass or are no-protocol demos;
13 load errors are unsupported mappers (out of scope by design).

T072 is closed. Remaining core gaps are scope items, not defects:
unsupported mappers (5+), PAL dot-level timing, and the MMC1 CHR-ROM
4K-window path being untested locally (no CHR-ROM MMC1 cartridge).
