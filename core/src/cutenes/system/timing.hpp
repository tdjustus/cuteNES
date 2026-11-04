#pragma once

#include <cstdint>

namespace cutenes::timing {

// Named timing constants (see specs/001-emulation-core/analysis/TIMING_MODEL.md,
// inventory items 1-11). Each value is the one the current behavior uses;
// changing one is a behavior change and must be justified by the tests it
// touches. Do not add a new unnamed offset elsewhere.

// CPU phi2 split: a CPU cycle is advanced in this many equal halves around a
// bus access (item 1).
inline constexpr std::uint32_t kCpuHalvesPerCycle = 2;

// PPU writes ignored after reset, in CPU cycles (research.md D4; item 5).
inline constexpr std::uint64_t kPostResetWriteIgnoreCpuCycles = 29658;

// PPU VBL edges, in PPU dots (items 3, 4). A $2002 read sees an edge once
// its dot has run. The read race and the NMI suppression follow from that
// and from the CPU's once-per-cycle NMI sample; they have no windows of their
// own (STATUS_READ_MODEL.md, #42).
inline constexpr int kVblSetDot = 1;        // VBL flag rises at (241, 1)
inline constexpr int kVblClearDot = 1;      // pre-render clears at (261, 1)

// The CPU samples the NMI line this many master ticks before a cycle ends,
// with the PPU caught up to that instant (item 2). On NTSC any value from 1
// to 4 lets the sample see exactly one PPU dot more than the cycle's bus
// access, which is what vbl_nmi_timing 7 and ppu_vbl_nmi 05-08 need; 1 is
// Mesen2's arrangement (edge detection at the cycle's end, the PPU one
// master tick behind).
inline constexpr std::uint64_t kNmiSampleLeadTicks = 1;

// The odd-frame skip decision: rendering must be enabled when this dot of
// the pre-render line ends, before any CPU write can land ahead of the next
// dot (ppu_vbl_nmi 10-even_odd_timing; item 3a). The dot after it is
// skipped.
inline constexpr int kOddFrameSkipDecisionDot = 338;

// Frame sequencer checkpoints, in CPU cycles from the sequence base (item 6).
// The 5-step table ends with the wrap point at 37281.
inline constexpr std::uint64_t kFrameSeq4[4] = {7457, 14913, 22371, 29830};
inline constexpr std::uint64_t kFrameSeq5[5] = {7457, 14913, 22371, 29829, 37281};

// Frame IRQ flag timing (items 7-10). The frame counter is clocked by the
// CPU, so these are CPU half-cycles of the region (6 master ticks on NTSC,
// 8 on PAL; #178): the flag-setting window is the same number of CPU cycles
// on both (nesdev wiki "APU Frame Counter", Mesen2's ApuFrameCounter).
inline constexpr std::uint64_t kFrameIrqVisibleTicks = 0;        // flag set at the APU boundary at/after the event (4-jitter 4/5)
inline constexpr std::uint64_t kFrameIrqAssertHalfCycles = 5;    // IRQ condition held after set
inline constexpr std::uint64_t kFrameIrqReassertHalfCycles = 1;  // read-race re-assert
inline constexpr std::uint64_t kFrameSeq5ExtraHalfCycles = 1;    // the 5-step period's half cycle past its last checkpoint

} // namespace cutenes::timing
