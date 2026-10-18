#include <cutenes/cutenes.hpp>

#include <gtest/gtest.h>

#include <cstring>

TEST(Version, PackedIdentifierMatchesString)
{
    // (0 << 22) | (1 << 12) | 0
    EXPECT_EQ(cutenes::coreVersion, 0x1000u);
    EXPECT_STREQ(cutenes::coreVersionString(), "0.1.0");
}

TEST(Version, CpuStateDefaultsMatchPowerOnState)
{
    // Guards the research.md D4 constants the machine applies at power-on.
    const cutenes::CpuState powerOn{};
    EXPECT_EQ(powerOn.p, 0x24);  // bit 5 reads 1; B flag never materialized
    EXPECT_EQ(powerOn.s, 0xFD);  // $00 - 3
}
