#include "cutenes/cartridge/cart.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

namespace {

// MMC1 board: 128 KiB PRG (8 x 16K, each bank's first byte = its index),
// 64 KiB CHR (16 x 4K pages, each page's first byte = its index), 8 KiB
// PRG-RAM.
std::vector<std::uint8_t> makeMmc1Rom()
{
    std::vector<std::uint8_t> rom{
        'N', 'E', 'S', 0x1A, 8, 8,
        0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    const std::size_t prgSize = 8 * 16384;
    const std::size_t chrSize = 16 * 4096;
    rom.resize(16 + prgSize + chrSize, 0);
    for (std::size_t bank = 0; bank < 8; ++bank) {
        rom[16 + bank * 16384] = static_cast<std::uint8_t>(bank);
    }
    for (std::size_t page = 0; page < 16; ++page) {
        rom[16 + prgSize + page * 4096] = static_cast<std::uint8_t>(page);
    }
    return rom;
}

struct Mmc1Harness {
    std::vector<std::uint8_t> rom = makeMmc1Rom();
    cutenes::Cartridge cart;

    Mmc1Harness()
    {
        const auto result = cutenes::Cartridge::load(rom.data(), rom.size(), cart);
        EXPECT_EQ(result.error, cutenes::LoadError::Ok);
    }

    // The 5-bit serial protocol: five writes, LSB first, the fifth commits.
    void serialWrite(std::uint16_t addr, std::uint8_t value)
    {
        for (int bit = 0; bit < 5; ++bit) {
            cart.cpuWrite(addr, static_cast<std::uint8_t>((value >> bit) & 1));
        }
    }

    std::uint8_t prgAt(std::uint16_t addr) { return cart.cpuRead(addr); }
};

} // namespace

// --- MMC1 shift-latch protocol ---------------------------------------------

TEST(Mmc1, FifthWriteCommitsNotBitZeroOfEachWrite)
{
    Mmc1Harness h;
    // Value 2 = 0b00010: first four writes carry bit 0 = 0, so nothing
    // may commit until the fifth write.
    h.cart.cpuWrite(0xE000, 0);
    h.cart.cpuWrite(0xE000, 1);
    h.cart.cpuWrite(0xE000, 0);
    h.cart.cpuWrite(0xE000, 0);
    EXPECT_EQ(h.prgAt(0x8000), 0) << "committed before the fifth write";
    h.cart.cpuWrite(0xE000, 0);
    EXPECT_EQ(h.prgAt(0x8000), 2);
}

TEST(Mmc1, SerialWriteSelectsSwitchableLowBankInModeThree)
{
    Mmc1Harness h;
    h.serialWrite(0xE000, 3); // PRG mode 3 is the power-on default
    EXPECT_EQ(h.prgAt(0x8000), 3);
    EXPECT_EQ(h.prgAt(0xC000), 7) << "last bank is fixed in mode 3";
}

TEST(Mmc1, PrgModeTwoFixesFirstBank)
{
    Mmc1Harness h;
    h.serialWrite(0x8000, 0x08); // PRG mode 2 (bits 2-3 = 10)
    h.serialWrite(0xE000, 5);
    EXPECT_EQ(h.prgAt(0x8000), 0) << "first bank is fixed in mode 2";
    EXPECT_EQ(h.prgAt(0xC000), 5);
}

TEST(Mmc1, PrgModeZeroSwitchesThirtyTwoKilobytes)
{
    Mmc1Harness h;
    h.serialWrite(0x8000, 0x00); // PRG mode 0: 32K
    h.serialWrite(0xE000, 3);    // bit 0 ignored: pair 2/3
    EXPECT_EQ(h.prgAt(0x8000), 2);
    EXPECT_EQ(h.prgAt(0xC000), 3);
}

TEST(Mmc1, ResetWriteRestoresModeThree)
{
    Mmc1Harness h;
    h.serialWrite(0x8000, 0x00);
    h.cart.cpuWrite(0x8000, 0x80); // bit 7 resets shift and control
    h.serialWrite(0xE000, 4);
    EXPECT_EQ(h.prgAt(0x8000), 4);
    EXPECT_EQ(h.prgAt(0xC000), 7);
}

// --- MMC1 CHR banking ------------------------------------------------------

TEST(Mmc1, Chr8KModeUsesPairedFourKPages)
{
    Mmc1Harness h;
    h.serialWrite(0x8000, 0x00); // control bit 4 clear: one 8K bank
    h.serialWrite(0xA000, 5);    // bank bit 0 ignored: 5 & 0x1E = 4 -> pages 4 and 5
    EXPECT_EQ(h.cart.chrRead(0x0000), 4);
    EXPECT_EQ(h.cart.chrRead(0x1000), 5);
}

TEST(Mmc1, Chr4KModeBanksEachHalfIndependently)
{
    Mmc1Harness h;
    h.serialWrite(0x8000, 0x10); // control bit 4 set: two 4K banks
    h.serialWrite(0xA000, 4);
    h.serialWrite(0xC000, 7);
    EXPECT_EQ(h.cart.chrRead(0x0000), 4);
    EXPECT_EQ(h.cart.chrRead(0x1000), 7);
}

// --- MMC1 PRG-RAM protect --------------------------------------------------

TEST(Mmc1, PrgRamWriteProtectIsControlledByPrgBankRegisterBit4)
{
    Mmc1Harness h;
    h.serialWrite(0x8000, 0x10); // CHR 4K mode must not affect WRAM
    h.cart.cpuWrite(0x6000, 0xAA);
    EXPECT_EQ(h.cart.cpuRead(0x6000), 0xAA) << "WRAM writable with $E000 bit 4 clear";

    h.serialWrite(0xE000, 0x10); // $E000 bit 4 set: WRAM disabled
    h.cart.cpuWrite(0x6000, 0x55);
    EXPECT_EQ(h.cart.cpuRead(0x6000), 0xAA) << "WRAM write must be blocked";
}

// --- MMC1 mirroring --------------------------------------------------------

TEST(Mmc1, ControlBitsSelectVerticalOrHorizontalMirroring)
{
    Mmc1Harness h;
    h.serialWrite(0x8000, 0x02);
    EXPECT_EQ(h.cart.mirroring(), cutenes::Mirroring::Vertical);
    h.serialWrite(0x8000, 0x03);
    EXPECT_EQ(h.cart.mirroring(), cutenes::Mirroring::Horizontal);
}
