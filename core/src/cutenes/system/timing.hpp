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

// PPU VBL edges and the $2002 race window, in PPU dots (items 3, 4).
inline constexpr int kVblSetDot = 1;        // VBL flag rises at (241, 1)
inline constexpr int kVblClearDot = 1;      // pre-render clears at (261, 1)
inline constexpr int kVblRaceDotMax = 2;    // $2002 read here returns flag clear
inline constexpr int kNmiKillDotMin = 1;    // $2002 read here cancels this NMI
inline constexpr int kNmiKillDotMax = 4;

// Frame sequencer checkpoints, in CPU cycles from the sequence base (item 6).
// The 5-step table ends with the wrap point at 37281.
inline constexpr std::uint64_t kFrameSeq4[4] = {7457, 14913, 22371, 29830};
inline constexpr std::uint64_t kFrameSeq5[5] = {7457, 14913, 22371, 29829, 37281};

// Frame IRQ flag timing, in master clock ticks (item 7-10).
inline constexpr std::uint64_t kFrameIrqVisibleTicks = 0;    // flag set at the APU boundary at/after the event (4-jitter 4/5)
inline constexpr std::uint64_t kFrameIrqAssertTicks = 30;    // IRQ line held after set
inline constexpr std::uint64_t kFrameIrqPollTicks = 24;      // CPU IRQ poll lookahead
inline constexpr std::uint64_t kFrameIrqReassertTicks = 6;   // read-race re-assert

} // namespace cutenes::timing
