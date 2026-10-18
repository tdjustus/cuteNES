#pragma once

#include <cutenes/cartridge.hpp>

#include <cstdint>

namespace cutenes {

// Every constant below comes from the nesdev Cycle reference chart as
// recorded in research.md D2. Rates are integer rationals — never floating
// point (data-model.md integrity rule 3).
struct RegionProfile {
    std::uint64_t masterClockNum;        // master clock Hz == num / den
    std::uint64_t masterClockDen;
    std::uint32_t cpuDivisor;            // master ticks per CPU cycle
    std::uint32_t ppuDivisor;            // master ticks per PPU dot
    std::uint32_t scanlines;             // per frame, incl. pre-render
    std::uint32_t dots;                  // per scanline
    std::uint32_t vblankScanline;        // VBlank flag set at dot 1 here
    std::uint32_t preRenderScanline;     // VBlank flag cleared at dot 1
    std::uint32_t cpuCyclesPerFrameNum;  // cycles/frame == num / den
    std::uint32_t cpuCyclesPerFrameDen;
    std::uint32_t frameRateNum;          // fps == num / den
    std::uint32_t frameRateDen;
};

// Unspecified resolves to NTSC (data-model.md, Machine entity).
const RegionProfile& regionProfile(Region region);

// Whole master ticks in one nominal frame (dots * scanlines * ppuDivisor).
// The odd-frame dot skip is a rendering-gated PPU behavior (tasks T023/T034)
// and is deliberately not folded in here.
std::uint64_t masterTicksPerFrame(const RegionProfile& profile);

} // namespace cutenes
