#include "cutenes/bus/bus.hpp"

#include <gtest/gtest.h>

namespace {

struct FakeDevice final : cutenes::CpuBusDevice {
    std::uint8_t cpuRead(std::uint16_t addr) override
    {
        return static_cast<std::uint8_t>(addr & 0xFF);
    }
    void cpuWrite(std::uint16_t addr, std::uint8_t value) override
    {
        lastWrite = {addr, value};
    }
    std::pair<std::uint16_t, std::uint8_t> lastWrite{0, 0};
};

} // namespace

TEST(Bus, RamMirrorsEvery0x800)
{
    cutenes::Bus bus;
    bus.write(0x0000, 0xAA);
    EXPECT_EQ(bus.read(0x0800), 0xAA);
    bus.write(0x07FF, 0x55);
    EXPECT_EQ(bus.read(0x1FFF), 0x55);
    EXPECT_EQ(bus.ram()[0x7FF], 0x55); // direct backing store
}

TEST(Bus, PpuRegistersMirrorEvery8Bytes)
{
    FakeDevice ppu;
    cutenes::Bus bus;
    bus.attachPpu(&ppu);
    EXPECT_EQ(bus.read(0x2000), 0x00);
    EXPECT_EQ(bus.read(0x3FF8), 0x00); // & 0x2007 == 0x2000
    EXPECT_EQ(bus.read(0x2007), 0x07);
    bus.write(0x3FFF, 0x42); // decodes to $2007
    EXPECT_EQ(ppu.lastWrite.first, 0x2007u);
    EXPECT_EQ(ppu.lastWrite.second, 0x42u);
}

TEST(Bus, ApuIoRegionRoutesToAttachedDevice)
{
    FakeDevice apuIo;
    cutenes::Bus bus;
    bus.attachApuIo(&apuIo);
    EXPECT_EQ(bus.read(0x4015), 0x15);
    bus.write(0x4014, 0x33); // OAM DMA lives in this region
    EXPECT_EQ(apuIo.lastWrite.first, 0x4014u);
}

TEST(Bus, MissingDevicesReadOpenBusWithoutCrashing)
{
    cutenes::Bus bus;
    (void)bus.read(0x2002);  // no PPU attached yet
    (void)bus.read(0x8000);  // no cartridge attached yet
    SUCCEED();
}

TEST(Bus, OpenBusLatchesLastValue)
{
    cutenes::Bus bus;
    bus.write(0x0000, 0x77); // RAM write latches the bus value
    EXPECT_EQ(bus.read(0x4020), 0x77); // 0x4020-0x5FFF unmapped: open bus
}
