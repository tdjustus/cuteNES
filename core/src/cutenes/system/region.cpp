#include "cutenes/system/region.hpp"

namespace cutenes {
namespace {

// NTSC: master 21.477272 MHz (236,250,000/11), CPU = master/12, PPU = /4,
// 262 scanlines x 341 dots, 29780.5 CPU cycles/frame, 60.0988 fps.
constexpr RegionProfile kNtsc{
    236250000, 11,
    12, 4,
    262, 341,
    241, 261,
    59561, 2,
    600988, 10000,
};

// PAL: master 26.6017125 MHz, CPU = master/16, PPU = /5, 312 scanlines,
// 33247.5 CPU cycles/frame, 50.0070 fps. PAL dot-level PPU behavior is
// under-documented on nesdev (research.md D2 disagreement registry) — PAL
// acceptance is per-frame, not dot-level.
constexpr RegionProfile kPal{
    266017125, 10,
    16, 5,
    312, 341,
    241, 311,
    66495, 2,
    500070, 10000,
};

} // namespace

const RegionProfile& regionProfile(Region region)
{
    return region == Region::Pal ? kPal : kNtsc; // Unspecified => NTSC
}

std::uint64_t masterTicksPerFrame(const RegionProfile& profile)
{
    return static_cast<std::uint64_t>(profile.dots) * profile.scanlines
        * profile.ppuDivisor;
}

} // namespace cutenes
