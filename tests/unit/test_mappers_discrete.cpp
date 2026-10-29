// Discrete-logic mappers: UxROM (2), CNROM (3), AxROM (7), ColorDreams (11)
// and BNROM (34). Expectations come from the nesdev wiki register
// descriptions, not from the implementation.

#include "cutenes/cartridge/cart.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

namespace {

// Builds an iNES 1.0 ROM. Every 16K PRG bank's first byte is its bank index;
// every 1K CHR bank's first byte is its 1K bank index (mod 256). chrBanks8k
// of 0 means CHR-RAM.
std::vector<std::uint8_t> makeRom(std::uint16_t mapper, int prgBanks16,
    int chrBanks8, std::uint8_t flags6 = 0)
{
    std::vector<std::uint8_t> rom{
        'N', 'E', 'S', 0x1A, static_cast<std::uint8_t>(prgBanks16),
        static_cast<std::uint8_t>(chrBanks8),
        static_cast<std::uint8_t>(flags6 | ((mapper & 0x0F) << 4)),
        static_cast<std::uint8_t>(mapper & 0xF0), 0, 0, 0, 0, 0, 0, 0, 0};
    const std::size_t prg = static_cast<std::size_t>(prgBanks16) * 16384;
    const std::size_t chr = static_cast<std::size_t>(chrBanks8) * 8192;
    rom.resize(16 + prg + chr, 0);
    for (int bank = 0; bank < prgBanks16; ++bank) {
        rom[16 + static_cast<std::size_t>(bank) * 16384] =
            static_cast<std::uint8_t>(bank);
    }
    for (std::size_t bank = 0; bank < chr / 1024; ++bank) {
        rom[16 + prg + bank * 1024] = static_cast<std::uint8_t>(bank);
    }
    return rom;
}

struct Board {
    std::vector<std::uint8_t> rom;
    cutenes::Cartridge cart;

    Board(std::uint16_t mapper, int prgBanks16, int chrBanks8,
        std::uint8_t flags6 = 0)
        : rom(makeRom(mapper, prgBanks16, chrBanks8, flags6))
    {
        const auto result = cutenes::Cartridge::load(rom.data(), rom.size(), cart);
        EXPECT_EQ(result.error, cutenes::LoadError::Ok);
    }

    std::uint8_t prg(std::uint16_t addr) { return cart.cpuRead(addr); }
    // 8K CHR bank number seen at $0000 (1K tags advance by 8 per 8K bank).
    int chrBankAtZero() { return cart.chrRead(0x0000) / 8; }
    void write(std::uint8_t value) { cart.cpuWrite(0x8000, value); }
};

} // namespace

// --- UxROM (mapper 2) ---------------------------------------------------------
// $8000-$BFFF: switchable 16K bank; $C000-$FFFF: fixed to the last bank.

TEST(Uxrom, PowersOnWithBankZeroAndTheLastBankFixed)
{
    Board b(2, 8, 0);
    EXPECT_EQ(b.prg(0x8000), 0);
    EXPECT_EQ(b.prg(0xC000), 7);
}

TEST(Uxrom, WriteSelectsTheSwitchableBankAndLeavesTheFixedOne)
{
    Board b(2, 8, 0);
    b.write(3);
    EXPECT_EQ(b.prg(0x8000), 3);
    EXPECT_EQ(b.prg(0xC000), 7);
    b.write(6);
    EXPECT_EQ(b.prg(0x8000), 6);
}

TEST(Uxrom, WritesAnywhereInRomSpaceSelectTheBank)
{
    Board b(2, 8, 0);
    b.cart.cpuWrite(0xFFFF, 5);
    EXPECT_EQ(b.prg(0x8000), 5);
}

TEST(Uxrom, BankNumbersWrapToTheRomSize)
{
    Board b(2, 8, 0); // 128K: three bank bits are decoded
    b.write(9);
    EXPECT_EQ(b.prg(0x8000), 1);
}

TEST(Uxrom, UorOmReachesAllSixteenBanks)
{
    Board b(2, 16, 0); // 256K
    b.write(12);
    EXPECT_EQ(b.prg(0x8000), 12);
    EXPECT_EQ(b.prg(0xC000), 15);
}

TEST(Uxrom, ChrRamIsPlainEightKilobytes)
{
    Board b(2, 8, 0);
    b.cart.chrWrite(0x0123, 0x5A);
    b.cart.chrWrite(0x1FFF, 0xA5);
    EXPECT_EQ(b.cart.chrRead(0x0123), 0x5A);
    EXPECT_EQ(b.cart.chrRead(0x1FFF), 0xA5);
}

// --- CNROM (mapper 3) ---------------------------------------------------------
// PRG is fixed like NROM; a write selects the 8K CHR bank.

TEST(Cnrom, WriteSelectsTheEightKilobyteChrBank)
{
    Board b(3, 2, 4); // 32K PRG, 32K CHR = 4 banks
    EXPECT_EQ(b.chrBankAtZero(), 0);
    b.write(2);
    EXPECT_EQ(b.chrBankAtZero(), 2);
    b.write(3);
    EXPECT_EQ(b.chrBankAtZero(), 3);
}

TEST(Cnrom, ChrBanksSwitchTheWholePatternSpace)
{
    Board b(3, 2, 4);
    b.write(1);
    EXPECT_EQ(b.cart.chrRead(0x1C00), 1 * 8 + 7) << "last 1K of bank 1";
}

TEST(Cnrom, ChrBankNumbersWrapToTheChrSize)
{
    Board b(3, 2, 4);
    b.write(6); // only two bank bits are decoded for 32K
    EXPECT_EQ(b.chrBankAtZero(), 2);
}

TEST(Cnrom, PrgIsFixedAndASixteenKilobyteRomMirrors)
{
    Board b(3, 1, 4); // 16K PRG
    EXPECT_EQ(b.prg(0x8000), 0);
    EXPECT_EQ(b.prg(0xC000), 0) << "16K PRG appears in both halves";
    b.write(1);
    EXPECT_EQ(b.prg(0x8000), 0) << "a CHR write must not move PRG";
}

// --- AxROM (mapper 7) ---------------------------------------------------------
// A write selects a 32K PRG bank (bits 0-2); bit 4 picks the one-screen page.

TEST(Axrom, WriteSelectsAThirtyTwoKilobytePrgBank)
{
    Board b(7, 16, 0); // 256K = 8 x 32K
    b.write(5);
    EXPECT_EQ(b.prg(0x8000), 10) << "32K bank 5 = 16K banks 10, 11";
    EXPECT_EQ(b.prg(0xC000), 11);
}

TEST(Axrom, PowersOnWithTheFirstBank)
{
    Board b(7, 16, 0);
    EXPECT_EQ(b.prg(0x8000), 0);
    EXPECT_EQ(b.prg(0xC000), 1);
}

TEST(Axrom, BitFourDoesNotDisturbThePrgBank)
{
    Board b(7, 16, 0);
    b.write(0x13); // bank 3, nametable page 1
    EXPECT_EQ(b.prg(0x8000), 6);
}

// --- BNROM (mapper 34) --------------------------------------------------------
// A write selects a 32K PRG bank; CHR is 8K RAM.

TEST(Bnrom, WriteSelectsAThirtyTwoKilobytePrgBank)
{
    Board b(34, 8, 0); // 128K = 4 x 32K
    b.write(2);
    EXPECT_EQ(b.prg(0x8000), 4) << "32K bank 2 = 16K banks 4, 5";
    EXPECT_EQ(b.prg(0xC000), 5);
    b.write(1);
    EXPECT_EQ(b.prg(0x8000), 2);
    EXPECT_EQ(b.prg(0xC000), 3);
}

TEST(Bnrom, PowersOnWithTheFirstBank)
{
    Board b(34, 8, 0);
    EXPECT_EQ(b.prg(0x8000), 0);
    EXPECT_EQ(b.prg(0xC000), 1);
}

// --- ColorDreams (mapper 11) --------------------------------------------------
// Write CCCC LLPP: bits 0-1 select the 32K PRG bank, bits 4-7 the 8K CHR bank.

TEST(ColorDreams, LowBitsSelectPrgAndHighBitsSelectChr)
{
    Board b(11, 8, 8); // 128K PRG = 4 x 32K, 64K CHR = 8 banks
    b.write(0x52);     // CHR bank 5, PRG bank 2
    EXPECT_EQ(b.prg(0x8000), 4);
    EXPECT_EQ(b.prg(0xC000), 5);
    EXPECT_EQ(b.chrBankAtZero(), 5);
}

TEST(ColorDreams, PrgAndChrFieldsAreIndependent)
{
    Board b(11, 8, 8);
    b.write(0x03);
    EXPECT_EQ(b.prg(0x8000), 6);
    EXPECT_EQ(b.chrBankAtZero(), 0);
    b.write(0x70);
    EXPECT_EQ(b.prg(0x8000), 0);
    EXPECT_EQ(b.chrBankAtZero(), 7);
}
