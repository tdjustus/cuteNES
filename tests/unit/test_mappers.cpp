#include "cutenes/cartridge/cart.hpp"
#include "cutenes/system/scheduler.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

namespace {

// MMC1 board: 128 KiB PRG default (each bank's first byte = its index),
// 64 KiB CHR (16 x 4K pages, each page's first byte = its index), 8 KiB
// PRG-RAM. Mapper and PRG bank count are parameterized for the revision
// tests (mapper 155 = MMC1A; 16 banks = 256 KiB for the A17 bypass).
std::vector<std::uint8_t> makeMmc1Rom(std::uint16_t mapper = 1,
    int prgBanks = 8, int ramBanks = 0)
{
    std::vector<std::uint8_t> rom{
        'N', 'E', 'S', 0x1A,
        static_cast<std::uint8_t>(prgBanks), 8,
        0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    // Mapper number: low nibble in byte 6 high, high nibble in byte 7 bits
    // 7-4 (byte 7 bit 3 is the NES 2.0 flag — keep it clear).
    rom[6] = static_cast<std::uint8_t>((rom[6] & 0x0F) | (mapper << 4));
    rom[7] = static_cast<std::uint8_t>(mapper & 0xF0);
    rom[8] = static_cast<std::uint8_t>(ramBanks); // iNES PRG-RAM, 8K units
    const std::size_t prgSize = static_cast<std::size_t>(prgBanks) * 16384;
    const std::size_t chrSize = 16 * 4096;
    rom.resize(16 + prgSize + chrSize, 0);
    for (std::size_t bank = 0; bank < static_cast<std::size_t>(prgBanks); ++bank) {
        rom[16 + bank * 16384] = static_cast<std::uint8_t>(bank);
    }
    for (std::size_t page = 0; page < 16; ++page) {
        rom[16 + prgSize + page * 4096] = static_cast<std::uint8_t>(page);
    }
    return rom;
}

struct Mmc1Harness {
    std::vector<std::uint8_t> rom;
    cutenes::Cartridge cart;
    cutenes::Scheduler sched{cutenes::regionProfile(cutenes::Region::Ntsc)};

    explicit Mmc1Harness(std::uint16_t mapper = 1, int prgBanks = 8,
        int ramBanks = 0)
        : rom(makeMmc1Rom(mapper, prgBanks, ramBanks))
    {
        const auto result = cutenes::Cartridge::load(rom.data(), rom.size(), cart);
        EXPECT_EQ(result.error, cutenes::LoadError::Ok);
        cart.attachScheduler(sched);
    }

    // The 5-bit serial protocol: five writes, LSB first, the fifth commits.
    // Real serial writes are always at least two cycles apart (LDA/LSR/STA
    // sequences), so the harness spaces them 24 master ticks — exercising
    // the consecutive-write rule's non-consecutive side.
    void serialWrite(std::uint16_t addr, std::uint8_t value)
    {
        for (int bit = 0; bit < 5; ++bit) {
            sched.advanceMaster(24);
            cart.cpuWrite(addr, static_cast<std::uint8_t>((value >> bit) & 1));
        }
    }

    // A raw serial-port write with explicit cycle spacing (default: not
    // consecutive to the previous write).
    void write(std::uint16_t addr, std::uint8_t bit0, std::uint64_t gap = 24)
    {
        sched.advanceMaster(gap);
        cart.cpuWrite(addr, bit0);
    }

    std::uint8_t prgAt(std::uint16_t addr) { return cart.cpuRead(addr); }
};

} // namespace

// --- MMC1 shift-latch protocol ---------------------------------------------

TEST(Mmc1, FifthWriteCommitsNotBitZeroOfEachWrite)
{
    Mmc1Harness h;
    // Value 2 = 0b00010: first four writes carry bit 0 = 0, so nothing
    // may commit until the fifth write. h.write spaces the writes two
    // cycles apart (real instruction spacing) so the consecutive-write
    // rule does not apply.
    h.write(0xE000, 0);
    h.write(0xE000, 1);
    h.write(0xE000, 0);
    h.write(0xE000, 0);
    EXPECT_EQ(h.prgAt(0x8000), 0) << "committed before the fifth write";
    h.write(0xE000, 0);
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
    // While disabled the RAM is disconnected (reads return the floating
    // bus; this core's absent-RAM convention is 0) — verify the block by
    // re-enabling: the 0x55 write must not have landed.
    EXPECT_EQ(h.cart.cpuRead(0x6000), 0) << "disabled WRAM reads float";
    h.serialWrite(0xE000, 0x00); // bit 4 clear again
    EXPECT_EQ(h.cart.cpuRead(0x6000), 0xAA) << "WRAM write must be blocked";
}

// --- MMC1 ASIC revisions (nesdev MMC1, "ASIC Revisions") -------------------

TEST(Mmc1, RevisionBDisablesRamButRevisionAKeepsItEnabled)
{
    Mmc1Harness b;      // mapper 1 = MMC1B
    Mmc1Harness a{155}; // mapper 155 = MMC1A
    b.serialWrite(0xE000, 0x10); // $E000 bit 4: disable (B) / A17 bypass (A)
    a.serialWrite(0xE000, 0x10);

    b.cart.cpuWrite(0x6000, 0x5A);
    a.cart.cpuWrite(0x6000, 0xA5);
    EXPECT_EQ(b.cart.cpuRead(0x6000), 0) << "MMC1B: RAM disabled";
    EXPECT_EQ(a.cart.cpuRead(0x6000), 0xA5) << "MMC1A: RAM always enabled";
}

TEST(Mmc1, RevisionABitFourBypassesFixedBankA17)
{
    // 256 KiB PRG (16 banks) so bank bit 3 maps to PRG A17; mode 3 fixes
    // bank 15 at $C000. With the bypass bit set, register bit 3 drives A17
    // of the fixed window: bank register 3 (bit 3 clear) -> $C000 reads
    // bank 7; clearing the bypass restores bank 15.
    Mmc1Harness h{155, 16};
    h.serialWrite(0xE000, 0x03); // bank 3, bit 4 clear: no bypass
    EXPECT_EQ(h.prgAt(0x8000), 3);
    EXPECT_EQ(h.prgAt(0xC000), 15) << "fixed high bank unaffected";

    h.serialWrite(0xE000, 0x13); // bit 4 set: A17 bypass, bank 3
    EXPECT_EQ(h.prgAt(0x8000), 3) << "switchable bank unchanged";
    EXPECT_EQ(h.prgAt(0xC000), 7) << "fixed bank A17 driven by reg bit 3";

    h.serialWrite(0xE000, 0x1B); // bit 4 set, bank 11 (bit 3 set)
    EXPECT_EQ(h.prgAt(0xC000), 15) << "A17=1 restores the fixed bank";

    // The MMC1B ignores the bypass meaning: bit 4 only disables RAM.
    Mmc1Harness b;
    b.serialWrite(0xE000, 0x13);
    EXPECT_EQ(b.prgAt(0xC000), 7) << "last bank fixed on 128K either way";
}

TEST(Mmc1, ConsecutiveCycleWriteDataBitIgnoredButResetNever)
{
    Mmc1Harness h;
    h.serialWrite(0xE000, 3); // bank 3 in mode 3 (power-on default)
    EXPECT_EQ(h.prgAt(0x8000), 3);

    // RMW double write (INC $E000-style): $00 then $01 on back-to-back
    // cycles. The first write applies (shifts a 0), the second is ignored,
    // so reaching the fifth EFFECTIVE write takes six writes total.
    h.write(0xE000, 0, 24); // data 0, not consecutive
    h.write(0xE000, 1, 12); // next cycle: IGNORED (RMW's second write)
    h.write(0xE000, 0, 24);
    h.write(0xE000, 0, 24);
    h.write(0xE000, 0, 24);
    h.write(0xE000, 0, 24); // fifth effective write commits: value 0
    EXPECT_EQ(h.prgAt(0x8000), 0) << "the consecutive data bit was ignored";

    // Bill & Ted: reset via INC on a location holding $FF — the $FF write
    // resets, the following-cycle $00 write must not disturb the register.
    h.serialWrite(0x8000, 0x08); // mode 2: fix first bank at $8000
    EXPECT_EQ(h.prgAt(0x8000), 0) << "mode 2 fixes bank 0 low";
    h.write(0x8000, 0x80, 24);   // $80: bit-7 reset (mode forced to 3)
    h.write(0x8000, 0x00, 12);   // next cycle: ignored
    h.serialWrite(0xE000, 5);
    // The reset forced mode 3: the low window is switchable again, and the
    // ignored $00 must not have poisoned the fifth write's commit.
    EXPECT_EQ(h.prgAt(0x8000), 5) << "reset forced mode 3: low bank switchable";
    EXPECT_EQ(h.prgAt(0xC000), 7);

    // Shinsenden: RRA's second write CARRIES bit 7 — the reset must apply
    // even though the write is consecutive to the first.
    h.write(0xE000, 0, 24);   // RRA write 1: old value, data bit applies
    h.write(0xE000, 0x80, 12); // RRA write 2: bit-7 reset, never ignored
    h.serialWrite(0xE000, 4);
    EXPECT_EQ(h.prgAt(0x8000), 4) << "reset applied on a consecutive cycle";
    EXPECT_EQ(h.prgAt(0xC000), 7);
}

// --- MMC1 large boards (SUROM 512K PRG, SXROM 32K WRAM) -----------------------

// SUROM: with more than 256K of PRG, bit 4 of the CHR bank 0 register picks
// which 256K half every PRG window shows (it drives PRG A18); the fixed bank
// is the last bank of the selected half.
TEST(Mmc1, SuromChrBitFourSelectsThe256KPrgHalf)
{
    Mmc1Harness h(1, 32); // 512 KiB PRG: 32 x 16K, bank index in byte 0
    h.serialWrite(0xE000, 3);
    EXPECT_EQ(h.prgAt(0x8000), 3);
    EXPECT_EQ(h.prgAt(0xC000), 15) << "fixed bank of the lower half";

    h.serialWrite(0xA000, 0x10);
    EXPECT_EQ(h.prgAt(0x8000), 16 + 3);
    EXPECT_EQ(h.prgAt(0xC000), 31) << "fixed bank of the upper half";

    h.serialWrite(0xA000, 0x00);
    EXPECT_EQ(h.prgAt(0x8000), 3);
}

TEST(Mmc1, SuromHalfSelectAppliesInThirtyTwoKilobyteMode)
{
    Mmc1Harness h(1, 32);
    h.serialWrite(0x8000, 0x00); // PRG mode 0: 32K switchable (also 1-screen)
    h.serialWrite(0xE000, 4);    // 32K pair = banks 4,5
    h.serialWrite(0xA000, 0x10);
    EXPECT_EQ(h.prgAt(0x8000), 16 + 4);
    EXPECT_EQ(h.prgAt(0xC000), 16 + 5);
}

TEST(Mmc1, ChrBitFourDoesNotBankA256KBoard)
{
    Mmc1Harness h(1, 16); // 256 KiB: bit 4 has no PRG A18 to drive
    h.serialWrite(0xE000, 3);
    h.serialWrite(0xA000, 0x10);
    EXPECT_EQ(h.prgAt(0x8000), 3);
    EXPECT_EQ(h.prgAt(0xC000), 15);
}

// SXROM: with 32K of WRAM, CHR bank 0 bits 2-3 select the 8K bank at $6000.
TEST(Mmc1, SxromChrBitsSelectTheWramBank)
{
    Mmc1Harness h(1, 8, 4); // 4 x 8K = 32K PRG-RAM
    h.cart.cpuWrite(0x6000, 0x11);
    h.serialWrite(0xA000, 0x04); // WRAM bank 1
    EXPECT_EQ(h.cart.cpuRead(0x6000), 0x00) << "bank 1 is separate storage";
    h.cart.cpuWrite(0x6000, 0x22);
    h.serialWrite(0xA000, 0x0C); // WRAM bank 3
    EXPECT_EQ(h.cart.cpuRead(0x6000), 0x00);
    h.serialWrite(0xA000, 0x00); // back to bank 0
    EXPECT_EQ(h.cart.cpuRead(0x6000), 0x11);
    h.serialWrite(0xA000, 0x04);
    EXPECT_EQ(h.cart.cpuRead(0x6000), 0x22);
}

TEST(Mmc1, EightKilobyteWramIgnoresTheBankBits)
{
    Mmc1Harness h(1, 8, 1);
    h.cart.cpuWrite(0x6000, 0x33);
    h.serialWrite(0xA000, 0x0C);
    EXPECT_EQ(h.cart.cpuRead(0x6000), 0x33);
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

// One-screen mirroring: all four nametable slots must resolve to the same
// CIRAM page. Faking it with horizontal/vertical mirroring is wrong, because
// $2000/$2400 (or $2000/$2800) would then reach different pages.
namespace {

void expectOneScreen(const cutenes::Cartridge& cart,
    cutenes::Mapper::NtSource page)
{
    for (std::uint16_t slot = 0; slot < 4; ++slot) {
        EXPECT_EQ(cart.ntSource(static_cast<std::uint16_t>(0x2000 + slot * 0x400)),
            page) << "slot " << slot;
    }
}

} // namespace

TEST(Mmc1, OneScreenModesPinEveryNametableToOnePage)
{
    Mmc1Harness h;
    h.serialWrite(0x8000, 0x00); // control bits 0-1 = 0: one-screen, low page
    expectOneScreen(h.cart, cutenes::Mapper::NtSource::Ciram0);
    h.serialWrite(0x8000, 0x01); // 1: one-screen, high page
    expectOneScreen(h.cart, cutenes::Mapper::NtSource::Ciram1);
}

TEST(Mmc1, TwoScreenModesUseTheDefaultMapping)
{
    Mmc1Harness h;
    h.serialWrite(0x8000, 0x02); // vertical
    EXPECT_EQ(h.cart.ntSource(0x2000), cutenes::Mapper::NtSource::Default);
    h.serialWrite(0x8000, 0x03); // horizontal
    EXPECT_EQ(h.cart.ntSource(0x2400), cutenes::Mapper::NtSource::Default);
}

TEST(AxRom, BankBitFourSelectsTheSingleNametablePage)
{
    Mmc1Harness h(7, 8);
    h.cart.cpuWrite(0x8000, 0x00);
    expectOneScreen(h.cart, cutenes::Mapper::NtSource::Ciram0);
    h.cart.cpuWrite(0x8000, 0x10);
    expectOneScreen(h.cart, cutenes::Mapper::NtSource::Ciram1);
}

// Action 53 registers are selected by a write to $5000-$5FFF and written at
// $8000+. The select must reach the mapper through the cartridge.
TEST(Action53, OneScreenModesPinEveryNametableToOnePage)
{
    Mmc1Harness h(28, 8);
    h.cart.cpuWrite(0x5000, 0x80); // select the mode register
    h.cart.cpuWrite(0x8000, 0x02); // leave the power-on one-screen mode...
    EXPECT_EQ(h.cart.ntSource(0x2000), cutenes::Mapper::NtSource::Default);
    h.cart.cpuWrite(0x8000, 0x00); // ...and return: mode bits 0-1 = 0
    h.cart.cpuWrite(0x5000, 0x01); // select the inner-bank register
    h.cart.cpuWrite(0x8000, 0x00); // bit 4 clear: low page
    expectOneScreen(h.cart, cutenes::Mapper::NtSource::Ciram0);
    h.cart.cpuWrite(0x8000, 0x10); // bit 4 set: high page
    expectOneScreen(h.cart, cutenes::Mapper::NtSource::Ciram1);
}

TEST(Action53, TwoScreenModesUseTheDefaultMapping)
{
    Mmc1Harness h(28, 8);
    h.cart.cpuWrite(0x5000, 0x80);
    h.cart.cpuWrite(0x8000, 0x02); // vertical
    EXPECT_EQ(h.cart.ntSource(0x2000), cutenes::Mapper::NtSource::Default);
    EXPECT_EQ(h.cart.mirroring(), cutenes::Mirroring::Vertical);
    h.cart.cpuWrite(0x8000, 0x03); // horizontal
    EXPECT_EQ(h.cart.mirroring(), cutenes::Mirroring::Horizontal);
}

TEST(Action53, RegisterSelectDoesNotTakeOverPrgRam)
{
    Mmc1Harness h(28, 8, 1); // 8K PRG-RAM
    h.cart.cpuWrite(0x6000, 0x4D);
    EXPECT_EQ(h.cart.cpuRead(0x6000), 0x4D);
    h.cart.cpuWrite(0x5000, 0x80);
    h.cart.cpuWrite(0x6001, 0x2B);
    EXPECT_EQ(h.cart.cpuRead(0x6001), 0x2B);
}

TEST(Action53, RegisterSelectIsWriteOnly)
{
    Mmc1Harness h(28, 8);
    h.cart.cpuWrite(0x5000, 0x81);
    EXPECT_NE(h.cart.cpuRead(0x5000), 0x81)
        << "the select register cannot be read back";
}

// --- MMC3 PRG banking ---------------------------------------------------------

namespace {

// MMC3 board: 256 KiB PRG (32 x 8K, each bank's first byte = its index),
// 8 KiB CHR.
// chrBanks8k: CHR ROM size in 8K units (0 = CHR-RAM). Every 1K CHR bank's
// first byte holds its 1K bank index.
std::vector<std::uint8_t> makeMmc3Rom(int chrBanks8k = 1)
{
    std::vector<std::uint8_t> rom{
        'N', 'E', 'S', 0x1A, 16, static_cast<std::uint8_t>(chrBanks8k),
        0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    const std::size_t prgSize = 32 * 8192;
    const std::size_t chrSize = static_cast<std::size_t>(chrBanks8k) * 8192;
    rom.resize(16 + prgSize + chrSize, 0);
    for (std::size_t bank = 0; bank < 32; ++bank) {
        rom[16 + bank * 8192] = static_cast<std::uint8_t>(bank);
    }
    for (std::size_t bank = 0; bank < chrSize / 1024; ++bank) {
        rom[16 + prgSize + bank * 1024] = static_cast<std::uint8_t>(bank);
    }
    return rom;
}

struct Mmc3Harness {
    std::vector<std::uint8_t> rom;
    // submapper > 0 promotes the header to NES 2.0 (byte 7 bit 3, byte 13
    // low nibble) so mapper 4 can select the MMC3 revision.
    explicit Mmc3Harness(int submapper = 0, int chrBanks8k = 1)
        : rom(makeMmc3Rom(chrBanks8k))
    {
        if (submapper > 0) {
            rom[7] = static_cast<std::uint8_t>((rom[7] & 0xF0) | 0x08);
            rom[13] = static_cast<std::uint8_t>(submapper & 0x0F);
        }
        const auto result = cutenes::Cartridge::load(rom.data(), rom.size(), cart);
        EXPECT_EQ(result.error, cutenes::LoadError::Ok);
    }
    cutenes::Cartridge cart;

    void select(std::uint8_t reg, std::uint8_t data)
    {
        cart.cpuWrite(0x8000, reg);
        cart.cpuWrite(0x8001, data);
    }

    std::uint8_t prgAt(std::uint16_t addr) { return cart.cpuRead(addr); }
    std::uint8_t chrAt(std::uint16_t addr) { return cart.chrRead(addr); }
};

} // namespace

// --- MMC3 CHR banking ---------------------------------------------------------
// Normal layout (bank-select bit 7 = 0): R0/R1 are 2K banks at $0000/$0800
// (the low bit is ignored), R2-R5 are 1K banks at $1000/$1400/$1800/$1C00.
// Inverted (bit 7 = 1): R2-R5 fill $0000-$0FFF, R0/R1 sit at $1000/$1800.

TEST(Mmc3, ChrNormalLayoutPutsTwoKilobyteBanksLow)
{
    Mmc3Harness h(0, 8); // 64K CHR = 64 x 1K banks
    h.select(0, 8);
    h.select(1, 12);
    h.select(2, 20);
    h.select(3, 21);
    h.select(4, 22);
    h.select(5, 23);
    EXPECT_EQ(h.chrAt(0x0000), 8);
    EXPECT_EQ(h.chrAt(0x0400), 9) << "R0 covers two 1K banks";
    EXPECT_EQ(h.chrAt(0x0800), 12);
    EXPECT_EQ(h.chrAt(0x0C00), 13);
    EXPECT_EQ(h.chrAt(0x1000), 20);
    EXPECT_EQ(h.chrAt(0x1400), 21);
    EXPECT_EQ(h.chrAt(0x1800), 22);
    EXPECT_EQ(h.chrAt(0x1C00), 23);
}

TEST(Mmc3, ChrTwoKilobyteRegistersIgnoreTheirLowBit)
{
    Mmc3Harness h(0, 8);
    h.select(0, 9);  // odd: still the pair 8,9
    h.select(1, 13); // odd: still the pair 12,13
    EXPECT_EQ(h.chrAt(0x0000), 8);
    EXPECT_EQ(h.chrAt(0x0400), 9);
    EXPECT_EQ(h.chrAt(0x0800), 12);
    EXPECT_EQ(h.chrAt(0x0C00), 13);
}

TEST(Mmc3, ChrInvertedLayoutSwapsTheHalves)
{
    Mmc3Harness h(0, 8);
    h.cart.cpuWrite(0x8000, 0x80); // bank-select bit 7 set, register 0
    auto set = [&](std::uint8_t reg, std::uint8_t data) {
        h.cart.cpuWrite(0x8000, static_cast<std::uint8_t>(0x80 | reg));
        h.cart.cpuWrite(0x8001, data);
    };
    set(0, 8);
    set(1, 12);
    set(2, 20);
    set(3, 21);
    set(4, 22);
    set(5, 23);
    EXPECT_EQ(h.chrAt(0x0000), 20);
    EXPECT_EQ(h.chrAt(0x0400), 21);
    EXPECT_EQ(h.chrAt(0x0800), 22);
    EXPECT_EQ(h.chrAt(0x0C00), 23);
    EXPECT_EQ(h.chrAt(0x1000), 8);
    EXPECT_EQ(h.chrAt(0x1400), 9);
    EXPECT_EQ(h.chrAt(0x1800), 12);
    EXPECT_EQ(h.chrAt(0x1C00), 13);
}

TEST(Mmc3, ChrRamReadsSeeWhatWritesStored)
{
    Mmc3Harness h(0, 0); // CHR-RAM (8K)
    h.select(2, 3);      // 1K bank 3 at $1000
    h.cart.chrWrite(0x1000, 0x5A);
    EXPECT_EQ(h.chrAt(0x1000), 0x5A) << "read must use the same banking";
    h.select(2, 4);
    EXPECT_NE(h.chrAt(0x1000), 0x5A) << "a different bank, different storage";
    h.select(2, 3);
    EXPECT_EQ(h.chrAt(0x1000), 0x5A);
}

TEST(Mmc3, ModeZeroFixesSecondLastAndLastBanks)
{
    Mmc3Harness h;
    h.select(6, 5);
    h.select(7, 9);
    EXPECT_EQ(h.prgAt(0x8000), 5) << "$8000 = R6";
    EXPECT_EQ(h.prgAt(0xA000), 9) << "$A000 = R7";
    EXPECT_EQ(h.prgAt(0xC000), 30) << "$C000 = second-last (fixed)";
    EXPECT_EQ(h.prgAt(0xE000), 31) << "$E000 = last (fixed)";
}

TEST(Mmc3, ModeOneSwapsFirstAndThirdWindows)
{
    Mmc3Harness h;
    h.cart.cpuWrite(0x8000, 0x40 | 6);
    h.cart.cpuWrite(0x8001, 5);
    EXPECT_EQ(h.prgAt(0x8000), 30) << "$8000 = second-last in mode 1";
    EXPECT_EQ(h.prgAt(0xC000), 5) << "$C000 = R6 in mode 1";
    EXPECT_EQ(h.prgAt(0xE000), 31);
}

TEST(Mmc3, DataWriteUsesRegisterIndexFromBankSelect)
{
    Mmc3Harness h;
    // Data byte 7 must not select R7: the register is the last $8000 write.
    h.cart.cpuWrite(0x8000, 6);
    h.cart.cpuWrite(0x8001, 7);
    EXPECT_EQ(h.prgAt(0x8000), 7) << "R6 should hold the data";
    EXPECT_EQ(h.prgAt(0xA000), 31) << "R7 must be untouched";
}

TEST(Mmc3, PrgRegisterKeepsSixBits)
{
    Mmc3Harness h;
    // 0x35 = bits 0-5 are 53; on a 32-bank ROM that is bank 21. Masking to
    // four bits would give bank 5.
    h.select(6, 0x35);
    EXPECT_EQ(h.prgAt(0x8000), 21);
}

// --- MMC3 scanline IRQ -----------------------------------------------------------

TEST(Mmc3Irq, FiresOnTheClockAfterTheLatchCountsDown)
{
    Mmc3Harness h;
    h.cart.cpuWrite(0xC000, 3); // latch
    h.cart.cpuWrite(0xC001, 0); // reload on next clock
    h.cart.cpuWrite(0xE001, 0); // enable
    h.cart.clockIrq();          // reload: counter = 3
    h.cart.clockIrq();          // 2
    h.cart.clockIrq();          // 1
    EXPECT_FALSE(h.cart.irqLine()) << "fired before the counter reached 0";
    h.cart.clockIrq();          // 0 -> assert
    EXPECT_TRUE(h.cart.irqLine());
}

TEST(Mmc3Irq, DisableAcknowledgesAndBlocksFurtherAssertion)
{
    Mmc3Harness h;
    h.cart.cpuWrite(0xC000, 1);
    h.cart.cpuWrite(0xC001, 0);
    h.cart.cpuWrite(0xE001, 0);
    h.cart.clockIrq();
    h.cart.clockIrq();
    h.cart.clockIrq();          // counter hits 0 and asserts
    ASSERT_TRUE(h.cart.irqLine());
    h.cart.cpuWrite(0xE000, 0); // disable: acknowledges
    EXPECT_FALSE(h.cart.irqLine());
    h.cart.cpuWrite(0xC001, 0);
    h.cart.clockIrq();
    h.cart.clockIrq();
    EXPECT_FALSE(h.cart.irqLine()) << "asserted while disabled";
}

TEST(Mmc3Irq, EnableAloneDoesNotAssertUntilCounterReachesZero)
{
    Mmc3Harness h;
    h.cart.cpuWrite(0xC000, 5);
    h.cart.cpuWrite(0xC001, 0);
    h.cart.cpuWrite(0xE001, 0);
    EXPECT_FALSE(h.cart.irqLine());
    h.cart.clockIrq();
    EXPECT_FALSE(h.cart.irqLine());
}

TEST(Mmc3Irq, RevisionAAssertsOnChangeToZeroOnly)
{
    Mmc3Harness h(/*submapper=*/4);
    // Latch 0 + $C001: the write puts the counter above 127, so the reload
    // clock is a change to zero -> assert (rev A rule, nesdev wiki MMC3).
    h.cart.cpuWrite(0xC000, 0);
    h.cart.cpuWrite(0xC001, 0);
    h.cart.cpuWrite(0xE001, 0);
    h.cart.clockIrq();
    EXPECT_TRUE(h.cart.irqLine());
    h.cart.cpuWrite(0xE000, 0); // acknowledge (also disables)
    // Automatic reload of 0 onto 0 is NOT a change -> stays quiet.
    h.cart.clockIrq();
    EXPECT_FALSE(h.cart.irqLine());
    // A decrement reaching 0 is a change -> assert.
    h.cart.cpuWrite(0xE001, 0); // re-enable
    h.cart.cpuWrite(0xC000, 1);
    h.cart.clockIrq();          // auto-reload (counter 0) -> 1
    h.cart.clockIrq();          // 1 -> 0
    EXPECT_TRUE(h.cart.irqLine());
}

TEST(Mmc3Irq, ManualRevisionSwitchOverridesTheHeader)
{
    Mmc3Harness h; // plain iNES header => revision B by default
    h.cart.mapper->setRevisionA(true); // manual override, no re-load
    h.cart.cpuWrite(0xC000, 0);
    h.cart.cpuWrite(0xC001, 0);
    h.cart.cpuWrite(0xE001, 0);
    h.cart.clockIrq();          // reload 0xFF -> 0: change to zero
    EXPECT_TRUE(h.cart.irqLine());
    h.cart.cpuWrite(0xE000, 0); // acknowledge
    h.cart.clockIrq();          // auto reload 0 -> 0: no change
    EXPECT_FALSE(h.cart.irqLine()) << "revision A rule after manual switch";
    h.cart.cpuWrite(0xE001, 0); // re-enable
    h.cart.mapper->setRevisionA(false); // and back
    h.cart.clockIrq();          // revision B: counter 0 asserts
    EXPECT_TRUE(h.cart.irqLine());
}

TEST(Mmc3Irq, RevisionBIsThePlainInesDefault)
{
    Mmc3Harness h; // iNES 1.0 header: submapper 0 = revision B
    h.cart.cpuWrite(0xC000, 0);
    h.cart.cpuWrite(0xC001, 0);
    h.cart.cpuWrite(0xE001, 0);
    h.cart.clockIrq();
    EXPECT_TRUE(h.cart.irqLine()); // every clock with latch 0
    h.cart.cpuWrite(0xE000, 0);
    h.cart.clockIrq();
    EXPECT_FALSE(h.cart.irqLine()); // acknowledged and disabled
}

TEST(Mmc3Irq, ZeroLatchAssertsEveryClockWhenEnabled)
{
    Mmc3Harness h;
    h.cart.cpuWrite(0xC000, 0);
    h.cart.cpuWrite(0xC001, 0);
    h.cart.cpuWrite(0xE001, 0);
    // Revision B: the reload clock itself asserts when the latch is 0
    // (4-scanline_timing reload 0 = IRQ on the pre-render clock).
    h.cart.clockIrq();
    EXPECT_TRUE(h.cart.irqLine());
    h.cart.cpuWrite(0xE000, 0); // acknowledge (also disables)
    h.cart.clockIrq();
    EXPECT_FALSE(h.cart.irqLine()) << "disabled: no assertion";
    h.cart.cpuWrite(0xE001, 0); // re-enable
    h.cart.clockIrq();          // counter 0 again: asserts once more
    EXPECT_TRUE(h.cart.irqLine());
}
