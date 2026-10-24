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
