#include "cutenes/cartridge/cart.hpp"

#include <cutenes/cutenes.hpp>

#include <gtest/gtest.h>

#include <array>
#include <vector>

namespace {

// 16-byte iNES 1.0 header + minimal NROM-128 payload (16K PRG, 8K CHR).
std::vector<std::uint8_t> makeNrom(std::uint8_t prgBanks, std::uint8_t chrBanks)
{
    std::vector<std::uint8_t> rom{
        'N', 'E', 'S', 0x1A, prgBanks, chrBanks, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    rom.resize(16 + 16384u * prgBanks + 8192u * chrBanks, 0xA5);
    return rom;
}

std::vector<std::uint8_t> makeNes2(std::uint8_t prgBanks,
    std::uint8_t chrBanks, std::uint16_t mapper, std::uint8_t flags6 = 0,
    std::uint8_t prgRamShift = 0, std::uint8_t tv = 0,
    std::uint8_t chrRamShift = 0)
{
    // Byte 11 = CHR-RAM shift, byte 12 = TV system (nesdev NES 2.0).
    std::vector<std::uint8_t> rom{
        'N', 'E', 'S', 0x1A, prgBanks, chrBanks,
        static_cast<std::uint8_t>(flags6 | ((mapper & 0x0F) << 4)),
        static_cast<std::uint8_t>(0x08 | ((mapper & 0xF0))),
        static_cast<std::uint8_t>(mapper >> 8), 0x00, prgRamShift,
        chrRamShift, tv, 0x00, 0x00, 0x00};
    rom.resize(16 + 16384u * prgBanks + 8192u * chrBanks, 0x5A);
    return rom;
}

} // namespace

TEST(Ines, LoadsValidNrom128)
{
    auto rom = makeNrom(1, 1);
    cutenes::Cartridge cart;
    const auto result = cutenes::Cartridge::load(rom.data(), rom.size(), cart);
    ASSERT_EQ(result.error, cutenes::LoadError::Ok);
    EXPECT_EQ(cart.info.formatVersion, 0);
    EXPECT_EQ(cart.info.mapperId, 0);
    EXPECT_EQ(cart.info.prgSize, 16384u);
    EXPECT_EQ(cart.info.chrSize, 8192u);
    EXPECT_EQ(cart.info.region, cutenes::Region::Unspecified);
}

TEST(Ines, Nrom128MirrorsProgramWindow)
{
    auto rom = makeNrom(1, 1);
    rom[16] = 0x60; // first PRG byte
    cutenes::Cartridge cart;
    ASSERT_EQ(cutenes::Cartridge::load(rom.data(), rom.size(), cart).error,
        cutenes::LoadError::Ok);
    EXPECT_EQ(cart.cpuRead(0x8000), 0x60);
    EXPECT_EQ(cart.cpuRead(0xC000), 0x60); // mirrored in NROM-128
}

TEST(Ines, RejectsBadMagic)
{
    auto rom = makeNrom(1, 1);
    rom[0] = 'X';
    cutenes::Cartridge cart;
    const auto result = cutenes::Cartridge::load(rom.data(), rom.size(), cart);
    EXPECT_EQ(result.error, cutenes::LoadError::NotAnInesFile);
}

TEST(Ines, RejectsTruncatedFile)
{
    auto rom = makeNrom(2, 1);
    rom.resize(rom.size() - 1); // one byte short of declared banks
    cutenes::Cartridge cart;
    EXPECT_EQ(cutenes::Cartridge::load(rom.data(), rom.size(), cart).error,
        cutenes::LoadError::TruncatedFile);
}

TEST(Ines, RejectsZeroPrgBanks)
{
    auto rom = makeNrom(0, 1);
    cutenes::Cartridge cart;
    const auto result = cutenes::Cartridge::load(rom.data(), rom.size(), cart);
    EXPECT_EQ(result.error, cutenes::LoadError::InvalidBankCounts);
}

TEST(Ines, RejectsUnsupportedMapperNamingItsId)
{
    auto rom = makeNrom(1, 1);
    rom[6] = (rom[6] & 0x0F) | (9u << 4); // mapper 9 (MMC2) low nibble
    cutenes::Cartridge cart;
    const auto result = cutenes::Cartridge::load(rom.data(), rom.size(), cart);
    EXPECT_EQ(result.error, cutenes::LoadError::UnsupportedMapper);
    EXPECT_EQ(result.detail, 9); // the mapper id, per FR-001
}

TEST(Ines, ParsesFlags6)
{
    auto rom = makeNrom(1, 1);
    rom[6] = 0x03; // vertical mirroring + battery
    cutenes::Cartridge cart;
    ASSERT_EQ(cutenes::Cartridge::load(rom.data(), rom.size(), cart).error,
        cutenes::LoadError::Ok);
    EXPECT_EQ(cart.info.mirroring, cutenes::Mirroring::Vertical);
    EXPECT_TRUE(cart.info.battery);
}

TEST(Ines, TrainerIsSkippedNotMisreadAsPrg)
{
    auto rom = makeNrom(1, 0);
    rom.insert(rom.begin() + 16, 512, 0xEE); // trainer section
    rom[6] |= 0x04;                          // trainer flag
    cutenes::Cartridge cart;
    ASSERT_EQ(cutenes::Cartridge::load(rom.data(), rom.size(), cart).error,
        cutenes::LoadError::Ok);
    EXPECT_EQ(cart.info.prgSize, 16384u);
    EXPECT_EQ(cart.cpuRead(0x8000), 0xA5); // real PRG data, not trainer
}

TEST(Ines, ChrZeroMeansChrRam)
{
    auto rom = makeNrom(1, 0);
    cutenes::Cartridge cart;
    ASSERT_EQ(cutenes::Cartridge::load(rom.data(), rom.size(), cart).error,
        cutenes::LoadError::Ok);
    EXPECT_EQ(cart.info.chrSize, 0u); // CHR-RAM
    cart.chrWrite(0x0000, 0x77);      // CHR-RAM must be writable
    EXPECT_EQ(cart.chrRead(0x0000), 0x77);
}

TEST(Ines, INes10PrgRamCompatDefaultIs8K)
{
    auto rom = makeNrom(1, 1);
    cutenes::Cartridge cart;
    ASSERT_EQ(cutenes::Cartridge::load(rom.data(), rom.size(), cart).error,
        cutenes::LoadError::Ok);
    EXPECT_EQ(cart.info.prgRamSize, 8192u); // flags8 value 0 infers 8K (D10)
}

TEST(Nes2, ParsesFormatAndSizes)
{
    // Mapper 0 here; mappers 1-4 load successfully once US6 implements them
    // (tasks T053-T056) — until then the parser names them in `detail`,
    // which Ines.RejectsUnsupportedMapperNamingItsId covers for mapper 9.
    auto rom = makeNes2(2, 0, 0); // 32K PRG, CHR-RAM
    cutenes::Cartridge cart;
    const auto result = cutenes::Cartridge::load(rom.data(), rom.size(), cart);
    ASSERT_EQ(result.error, cutenes::LoadError::Ok)
        << "detail=" << result.detail;
    EXPECT_EQ(cart.info.formatVersion, 2);
    EXPECT_EQ(cart.info.mapperId, 0);
    EXPECT_EQ(cart.info.prgSize, 32768u);
}

TEST(Nes2, ParsesRegionAndPrgRamShift)
{
    auto rom = makeNes2(1, 1, 0, 0, 7, 1); // PAL, 64<<7 = 8K PRG-RAM
    cutenes::Cartridge cart;
    ASSERT_EQ(cutenes::Cartridge::load(rom.data(), rom.size(), cart).error,
        cutenes::LoadError::Ok);
    EXPECT_EQ(cart.info.region, cutenes::Region::Pal);
    EXPECT_EQ(cart.info.prgRamSize, 8192u);
}

TEST(Nes2, ChrRamShiftSizing)
{
    auto rom = makeNes2(1, 0, 0, 0, 0, 0, 9); // 64 << 9 = 32K CHR-RAM
    cutenes::Cartridge cart;
    ASSERT_EQ(cutenes::Cartridge::load(rom.data(), rom.size(), cart).error,
        cutenes::LoadError::Ok);
    // Externally visible via writability; size checked via wrap behavior:
    // writes at $0000 and $2000 alias in 8K, distinct in 32K.
    cart.chrWrite(0x0000, 0x11);
    cart.chrWrite(0x2000, 0x22);
    EXPECT_EQ(cart.chrRead(0x0000), 0x11);
    EXPECT_EQ(cart.chrRead(0x2000), 0x22); // distinct banks => 32K
}
