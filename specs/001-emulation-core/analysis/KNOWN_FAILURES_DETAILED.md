# Detailed Analysis of Known Failures

> Historical. Every failure analysed below has been fixed; the sections are kept
> as the diagnostic record. Current known issues live in the GitHub `1.0`
> milestone and `docs/test-corpus.md`.

## Resolution status

- RESOLVED `apu_test/7-dmc_basics`: `$4015` start fills an empty DMC sample buffer immediately; a restart resets the bit counter to 8 so a buffered byte is not drained early.
- RESOLVED `apu_test/8-dmc_rates`: DMC timer reload is `rate/2 - 1` APU cycles (the counter takes `reload + 1` ticks per output bit).
- RESOLVED `apu_test/1-len_ctr`: in 5-step mode the frame sequencer never scheduled its fifth checkpoint (`m_sequenceStep < 4` bound), so it stalled. The bound is now 5 for 5-step mode.
- RESOLVED (unified master-clock timing model; tier-1 gated) `apu_test/5-len_timing` (test 2 vs 3): the first mode-0 length clock must fall between two reads one CPU cycle apart. Matching both needs sub-cycle `$4017`-write-to-sequencer phase, tied to T037/T068. Nesdev values are kept.
- RESOLVED (same model; tier-1 gated) `ppu_vbl_nmi/03-vbl_clear_time`: see the PPU phase trade-off below.

## 1. ppu_vbl_nmi/03-vbl_clear_time
**Status**: Failing  
**Issue**: VBL flag cleared ~1 PPU clock earlier than expected  
**Expected**: 00-04 show V, 05-08 show '-' (VBL cleared around after test reading at row 05)  
**Actual**: 00-03 show V, 04+ show '-' (cleared 1 PPU clock early)

**nesdev Reference**: 
- VBlank flag ($2002 bit 7) is set at dot 1 of scanline 241 (pre-render? no, set at 241,1)
- VBlank flag is cleared at dot 1 of pre-render scanline (scanline 261)
- Reading $2002 clears the VBlank flag

**Investigation**: The test reads $2002 once per iteration after delays. We clear the flag at scanline 261 dot 1. The timing is off by ~1 PPU clock.

**Possible causes**:
- Dot counting off by 1 somewhere
- Order of operations in stepDot() when handling pre-render line clear
- Odd/even frame timing affecting the sequence

## 2. apu_test/1-len_ctr
**Status**: Failing at test #3  
**Issue**: "Problem with length table, timing, or $4015" on channel 1  
**Expected**: Length counter behavior correct for all channels

**nesdev Reference**: Length counter is clocked by frame counter on quarter/half frame steps. Writing $4015 clears length counter for disabled channels. Length table values.

**Investigation needed**: Check if our fix to clear length on disable is correct. Test fails on channel 1 specifically - could be pulse 2 specific issue.

## 3. apu_test/5-len_timing
**Status**: Failing at test #2  
**Issue**: "First length of mode 0 is too soon"  
**Expected**: Length counter clocks at specific frame counter timings

**nesdev Reference**: Frame counter 4-step mode clocks at 7457, 14913, 22371, 29829 CPU cycles. Length counter clocked on steps 2,4 (half frame). Timing is critical.

**Investigation needed**: Verify our sequencePoint timing and when halfFrame() is called.

## 4. apu_test/7-dmc_basics
**Status**: Failing at test #19  
**Issue**: "There should be a one-byte buffer that's filled immediately if empty"  
**Expected**: DMC should have a one-byte buffer

**nesdev Reference**: DMC has a 1-byte sample buffer. When empty and sample bytes remain, DMC needs to fetch next byte. The CPU is halted for DMC DMA.

**Investigation needed**: DMC buffer/fetch timing and interaction with CPU.

## 5. apu_test/8-dmc_rates
**Status**: Failing at test #3  
**Issue**: "Rate 0's period is too long"  
**Expected**: DMC rate periods match hardware

**nesdev Reference**: DMC rate table values in CPU cycles.

**Investigation needed**: Verify kDmcRates values are correct for NTSC.

## Comparison with nesdev Documentation

### VBL Clear Timing
From nesdev: "The VBlank flag is cleared by reading $2002 or by writing to $2005 or $2006. It is also cleared at the end of vblank (at dot 1 of the pre-render line)."
Also: "VBL flag of $2002 is cleared after reading it, on the same dot."

Our implementation clears at pre-render line dot 1. The test expects it cleared 1 PPU clock later than we do. We need to move it to dot 2 or adjust timing.

### DMC Rates (NTSC)
From nesdev APU DMC page: NTSC DMC rate periods (CPU cycles): 428, 380, 340, 320, 286, 254, 226, 214, 190, 160, 142, 128, 106, 84, 72, 54
Our values match exactly. But test says "Rate 0's period is too long" - suggests our timer is counting differently.

Looking at our code: `m_dmc.timer = kDmcRates[m_dmc.rateIndex] / 2;` because DMC runs at CPU/2? Or the division is wrong.

### Length Counter
Frame counter timing is critical. We need to verify against exact hardware behavior.

## Fix Plan

### Priority 1: DMC Rates (8-dmc_rates)
- Issue: "Rate 0's period is too long"
- Our code divides kDmcRates by 2: `m_dmc.timer = kDmcRates[m_dmc.rateIndex] / 2;`
- DMC timer decrements every APU cycle (2 CPU cycles) in our implementation
- If kDmcRates are in CPU cycles, and we divide by 2 for APU cycles, that gives correct period
- But test says period is too long - meaning our timer is running slower or counting more cycles
- Try removing the /2 division

### Priority 2: DMC Basics (7-dmc_basics)
- Issue: Test 19 "There should be a one-byte buffer that's filled immediately if empty"
- Need to understand DMC buffer behavior better
- When DMC is enabled and buffer is empty and bytes remain, it should request a fetch

### Priority 3: PPU VBL Clear (03-vbl_clear_time)
- Clear VBL at dot 2 of pre-render line instead of dot 1?

### Priority 4: APU Length Timing
- Frame counter timing issues
- Length counter clocking

### Priority 5: APU Length Counter (1-len_ctr)
- Test fails on channel 1
