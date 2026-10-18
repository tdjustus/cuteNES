#include "cutenes/system/region.hpp"
#include "cutenes/system/scheduler.hpp"

#include <cutenes/cartridge.hpp>

#include <gtest/gtest.h>

// All expected values are the nesdev Cycle reference chart constants
// recorded in research.md D2.

TEST(Region, NtscProfileMatchesCycleReferenceChart)
{
    const auto& p = cutenes::regionProfile(cutenes::Region::Ntsc);
    EXPECT_EQ(p.masterClockNum, 236250000u); // 21.477272 MHz = 236.25 MHz / 11
    EXPECT_EQ(p.masterClockDen, 11u);
    EXPECT_EQ(p.cpuDivisor, 12u); // CPU = master / 12
    EXPECT_EQ(p.ppuDivisor, 4u);  // PPU = master / 4
    EXPECT_EQ(p.scanlines, 262u);
    EXPECT_EQ(p.dots, 341u);
    EXPECT_EQ(p.vblankScanline, 241u);     // flag set at dot 1
    EXPECT_EQ(p.preRenderScanline, 261u);  // flag cleared at dot 1
    EXPECT_EQ(p.cpuCyclesPerFrameNum, 59561u); // 29780.5
    EXPECT_EQ(p.cpuCyclesPerFrameDen, 2u);
    EXPECT_EQ(p.frameRateNum, 600988u);    // 60.0988 fps
    EXPECT_EQ(p.frameRateDen, 10000u);
}

TEST(Region, PalProfileMatchesCycleReferenceChart)
{
    const auto& p = cutenes::regionProfile(cutenes::Region::Pal);
    EXPECT_EQ(p.cpuDivisor, 16u);
    EXPECT_EQ(p.ppuDivisor, 5u);
    EXPECT_EQ(p.scanlines, 312u);
    EXPECT_EQ(p.preRenderScanline, 311u);
    EXPECT_EQ(p.cpuCyclesPerFrameNum, 66495u); // 33247.5
    EXPECT_EQ(p.cpuCyclesPerFrameDen, 2u);
    EXPECT_EQ(p.frameRateNum, 500070u);        // 50.0070 fps
    // 341 * 312 * 5 = 531960 master ticks per frame
    EXPECT_EQ(cutenes::masterTicksPerFrame(p), 531960u);
}

TEST(Region, UnspecifiedResolvesToNtscDefault)
{
    const auto& unspecified = cutenes::regionProfile(cutenes::Region::Unspecified);
    const auto& ntsc = cutenes::regionProfile(cutenes::Region::Ntsc);
    EXPECT_EQ(&unspecified, &ntsc);
}

TEST(Region, NtscMasterTicksPerFrame)
{
    EXPECT_EQ(cutenes::masterTicksPerFrame(
                  cutenes::regionProfile(cutenes::Region::Ntsc)),
        341u * 262u * 4u); // 357368
}

TEST(Scheduler, AdvancesMonotonically)
{
    cutenes::Scheduler s(cutenes::regionProfile(cutenes::Region::Ntsc));
    EXPECT_EQ(s.now(), 0u);
    s.advanceMaster(84u); // the 7-cycle reset window (research.md D4)
    EXPECT_EQ(s.now(), 84u);
}

TEST(Scheduler, CpuCycleConversionIsExact)
{
    const cutenes::Scheduler s(cutenes::regionProfile(cutenes::Region::Ntsc));
    EXPECT_EQ(s.cpuCycleToMaster(1u), 12u);
    EXPECT_EQ(s.cpuCycleToMaster(7u), 84u);
}

TEST(Scheduler, EndFrameAdvancesToExactBoundary)
{
    cutenes::Scheduler s(cutenes::regionProfile(cutenes::Region::Ntsc));
    s.advanceMaster(84u);
    s.endFrame();
    EXPECT_EQ(s.frameIndex(), 1u);
    EXPECT_EQ(s.now(), 357368u); // exactly one frame boundary past 0
    s.endFrame();
    EXPECT_EQ(s.frameIndex(), 2u);
    EXPECT_EQ(s.now(), 2u * 357368u);
}
