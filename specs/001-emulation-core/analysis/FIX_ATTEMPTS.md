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
