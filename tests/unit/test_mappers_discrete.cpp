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

// --- VRC2a (mapper 22) --------------------------------------------------------
// Board 351618 swaps the ASIC's register-select lines: sub-registers decode
// at $x000, $x002, $x001, $x003. PRG: $8000/$A000 = two 8K banks, $C000-$FFFF
// = the last 16K. CHR: eight 1K windows, each a two-write nibble register,
// and VRC2a drops the low bit of the CHR bank number (bank = value >> 1).

namespace {

// 128K PRG = 16 x 8K banks and 128K CHR = 128 x 1K banks. makeRom tags 16K
// banks; re-tag every 8K bank with its own index so 8K windows are checkable.
struct Vrc2a : Board {
    Vrc2a() : Board(22, 8, 16)
    {
        for (std::size_t bank = 0; bank < 16; ++bank) {
            rom[16 + bank * 8192] = static_cast<std::uint8_t>(bank);
        }
        cart = cutenes::Cartridge{};
        const auto result = cutenes::Cartridge::load(rom.data(), rom.size(), cart);
        EXPECT_EQ(result.error, cutenes::LoadError::Ok);
    }

    // Writes the 8-bit CHR value for window `n` (0-7) as two nibble writes.
    void setChr(int n, std::uint8_t value)
    {
        const std::uint16_t base =
            static_cast<std::uint16_t>(0xB000 + (n / 2) * 0x1000);
        const int odd = n & 1; // reg 1 of the pair uses $x001/$x003
        cart.cpuWrite(static_cast<std::uint16_t>(base + odd), value & 0x0F);
        cart.cpuWrite(static_cast<std::uint16_t>(base + 2 + odd), value >> 4);
    }
    int chrBank(int window) { return cart.chrRead(static_cast<std::uint16_t>(window * 0x400)); }
};

} // namespace

TEST(Vrc2a, PrgSelectsTwoEightKilobyteWindowsAndFixesTheLastSixteenK)
{
    Vrc2a b;
    b.cart.cpuWrite(0x8000, 3);
    b.cart.cpuWrite(0xA000, 5);
    EXPECT_EQ(b.prg(0x8000), 3);
    EXPECT_EQ(b.prg(0xA000), 5);
    EXPECT_EQ(b.prg(0xC000), 14) << "second-to-last 8K bank";
    EXPECT_EQ(b.prg(0xE000), 15) << "last 8K bank";
}

TEST(Vrc2a, PrgBankNumbersWrapToTheRomSize)
{
    Vrc2a b;
    b.cart.cpuWrite(0x8000, 0x13); // 19 wraps to 3 on 16 banks
    EXPECT_EQ(b.prg(0x8000), 3);
}

TEST(Vrc2a, MirroringRegisterSelectsVerticalOrHorizontal)
{
    Vrc2a b;
    b.cart.cpuWrite(0x9000, 0);
    EXPECT_EQ(b.cart.mirroring(), cutenes::Mirroring::Vertical);
    b.cart.cpuWrite(0x9000, 1);
    EXPECT_EQ(b.cart.mirroring(), cutenes::Mirroring::Horizontal);
    b.cart.cpuWrite(0x9000, 2); // VRC2 has no one-screen modes; bit 1 ignored
    EXPECT_EQ(b.cart.mirroring(), cutenes::Mirroring::Vertical);
}

TEST(Vrc2a, ChrBankIsTheTwoNibbleValueShiftedRightOnce)
{
    Vrc2a b;
    b.setChr(1, 0x00);
    EXPECT_EQ(b.chrBank(1), 0);
    b.setChr(1, 0x02);
    EXPECT_EQ(b.chrBank(1), 1);
    b.setChr(1, 0x03);
    EXPECT_EQ(b.chrBank(1), 1) << "the low bit is not wired";
    b.setChr(1, 0x35);
    EXPECT_EQ(b.chrBank(1), 0x1A);
    b.setChr(1, 0xFE);
    EXPECT_EQ(b.chrBank(1), 0x7F) << "reaches the last of 128 banks";
}

TEST(Vrc2a, EachOfTheEightWindowsHasItsOwnRegister)
{
    Vrc2a b;
    for (int n = 0; n < 8; ++n) {
        b.setChr(n, static_cast<std::uint8_t>((10 + n) * 2));
    }
    for (int n = 0; n < 8; ++n) {
        EXPECT_EQ(b.chrBank(n), 10 + n) << "window " << n;
    }
}

TEST(Vrc2a, NibbleWritesKeepTheOtherNibble)
{
    Vrc2a b;
    b.setChr(0, 0xA6);          // value 0xA6 -> bank 0x53
    b.cart.cpuWrite(0xB000, 0x8); // low nibble only -> 0xA8 -> bank 0x54
    EXPECT_EQ(b.chrBank(0), 0x54);
    b.cart.cpuWrite(0xB002, 0x1); // high nibble only -> 0x18 -> bank 0x0C
    EXPECT_EQ(b.chrBank(0), 0x0C);
}
