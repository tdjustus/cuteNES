# Test Analysis & Bug Resolution Plan

> Historical (early 2026-10 plan). All five failures listed were fixed; see
> `FIX_ATTEMPTS.md`. The method below is still how new failures are analysed.

**Goal**: Verify each failing test against nesdev/hardware documentation, confirm accuracy of test expectations, and resolve all bugs.

## Current Known Failures
1. `ppu_vbl_nmi/rom_singles/03-vbl_clear_time` - VBL flag clear timing
2. `apu_test/rom_singles/1-len_ctr` - Length counter behavior
3. `apu_test/rom_singles/5-len_timing` - Length counter frame timing
4. `apu_test/rom_singles/7-dmc_basics` - DMC basics
5. `apu_test/rom_singles/8-dmc_rates` - DMC rate periods

## Analysis Approach
For each test:
1. Read test source code to understand exact sequence/timings
2. Consult nesdev references (PPU registers, APU, frame counter, DMC)
3. Compare against expected behavior
4. Trace emulator code path
5. Add targeted fixes
6. Verify

## References
- https://www.nesdev.org/wiki/PPU_registers
- https://www.nesdev.org/wiki/APU_Frame_Counter
- https://www.nesdev.org/wiki/APU_DMC
- https://www.nesdev.org/wiki/APU_Length_Counter

## Resolution Strategy
Fix in order of complexity, verify each step.
