# CPU / APU / PPU phase model (T037, T068)

> Historical: the two failures named here were fixed (see TIMING_MODEL.md, which
> records the model that shipped). The one remaining phase-related gap is
> `vbl_nmi_timing/7.nmi_timing` (issue #42, `docs/test-corpus.md`).

Scope: the two open known failures, `ppu_vbl_nmi/03-vbl_clear_time` and
`apu_test/5-len_timing`. Both depend on sub-cycle ordering between a CPU bus access,
the PPU dot it lands on, and the APU frame-sequencer step.

## Current facts (from code)

- One master clock (`Scheduler::now()`). NTSC: 1 CPU cycle = `cpuDivisor` master ticks,
  1 PPU dot = `ppuDivisor` master ticks, 1 APU cycle = 2 CPU cycles.
- `Cpu::tickHalf` advances half a CPU cycle (`cpuDivisor / 2` master ticks, i.e. 1.5 PPU
  dots), then runs `Ppu::runTo` and `Apu::runTo` to that time.
- A CPU bus access is: tickHalf, bus access, sampleNmi, tickHalf. So reads and writes
  land at master offset `cpuDivisor/2` inside their cycle (mid-cycle, a PPU-dot
  boundary is not guaranteed).
- `Apu::runTo` steps the channels once per APU cycle (every `2 * cpuDivisor` master
  ticks). The frame sequencer is advanced inside the same step by adding 2 to
  `m_sequenceCycle` per APU step and comparing with the checkpoint table.

## Where the phase is lost

1. A `$4017` write resets the sequencer at the write time (mid-cycle master), but the
   sequencer only observes time in 2-CPU-cycle steps. The first checkpoint therefore
   fires at the first APU boundary after the checkpoint count, which is always an even
   CPU offset from the APU grid.
2. Hardware checkpoints are placed at exact CPU-cycle offsets from the write (odd
   offsets relative to the APU grid are reachable when the write parity is odd).
   Test 2 and test 3 of `5-len_timing` need a clock between two reads one CPU cycle apart,
   which needs an odd offset.
3. `03-vbl_clear_time` needs the `$2002` sample and the pre-render VBL clear to share a
   dot phase. The PPU clear is at dot-aligned master time, while the read sample is at
   a mid-cycle master time (1.5 dots from the cycle start).

## Model (target)

- Frame-sequencer checkpoints are scheduled in master ticks, not APU steps:
  `checkpointMaster = seqBaseMaster + cpuDivisor * k[step]`.
- `seqBaseMaster` is the master time of the `$4017` write (exact, mid-cycle) and advances
  by one full period at each sequence wrap, so the sequence keeps its phase.
- `Apu::runTo(masterClock)` interleaves: step channel APU cycles whose boundary is at or
  before the next checkpoint, fire every checkpoint at or before `masterClock`, then
  step the remaining APU cycles.
- The frame-IRQ visibility timestamp is quantized to the APU boundary as before, so the
  6-irq_flag_timing calibration is kept until measured otherwise.

## Stopping rule

Implement the target model once. Run `ctest --test-dir build -LE known-failing` and the
two known-failing ROMs. If both pass, promote them. If `5-len_timing` passes and
`03-vbl_clear_time` does not (or vice versa), keep the change only if no tier-1 test
regresses; otherwise revert and record the result in `FIX_ATTEMPTS.md`. Stop after this
single implementation pass and do not start a second model without a new design.

## Result (implementation pass 1)

Implemented as above (see FIX_ATTEMPTS.md L9). Outcome: `5-len_timing` still fails
test 2; `6-irq_flag_timing` and `4-jitter` regress. Per the stopping rule the change was
reverted. The regressions point at the frame-IRQ visibility constants (+24 / +30),
which were calibrated against APU-boundary timing and need their own calibration pass
before this model can land.

## Non-goals

- DMC DMA alignment (T037 DMC stall details) — separate change.
- Changing the NMI edge model (`Cpu::sampleNmi`) — only if the PPU result requires it.
