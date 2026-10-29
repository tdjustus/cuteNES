#include "cutenes/ppu/ppu.hpp"

#include "cutenes/cartridge/cart.hpp"
#include "cutenes/system/scheduler.hpp"

#include <gtest/gtest.h>

#include <vector>

namespace {

std::vector<std::uint8_t> makeNrom(bool verticalMirroring)
{
    std::vector<std::uint8_t> rom{
        'N', 'E', 'S', 0x1A, 1, 1,
        static_cast<std::uint8_t>(verticalMirroring ? 0x01 : 0x00),
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    rom.resize(16 + 16384 + 8192, 0);
    return rom;
}

struct PpuHarness {
    cutenes::Scheduler scheduler{cutenes::regionProfile(cutenes::Region::Ntsc)};
    cutenes::Cartridge cart;
    cutenes::Ppu ppu{cutenes::regionProfile(cutenes::Region::Ntsc), scheduler};

    explicit PpuHarness(bool vertical = false)
    {
        const auto rom = makeNrom(vertical);
        (void)cutenes::Cartridge::load(rom.data(), rom.size(), cart);
        ppu.attachCartridge(&cart);
        // NMI is now a sampled level line (nmiLine()); the
        // edge detection lives in the CPU.
        ppu.reset(/*cold=*/true);
        scheduler.reset();
    }

    // $2006/$2007 access helper (after the post-reset write-ignore window).
    void ppuWrite(std::uint16_t addr, std::uint8_t value)
    {
        scheduler.advanceMaster(29700 * 12); // clear the ignore window
        ppu.cpuWrite(0x2006, static_cast<std::uint8_t>(addr >> 8));
        ppu.cpuWrite(0x2006, static_cast<std::uint8_t>(addr & 0xFF));
        ppu.cpuWrite(0x2007, value);
    }

    std::uint8_t ppuRead(std::uint16_t addr)
    {
        scheduler.advanceMaster(29700 * 12);
        ppu.cpuWrite(0x2006, static_cast<std::uint8_t>(addr >> 8));
        ppu.cpuWrite(0x2006, static_cast<std::uint8_t>(addr & 0xFF));
        (void)ppu.cpuRead(0x2002); // clear latch state
        if (addr >= 0x3F00) {
            return ppu.cpuRead(0x2007); // palette reads are immediate
        }
        (void)ppu.cpuRead(0x2007); // fill the VRAM read buffer
        return ppu.cpuRead(0x2007);
    }
};

constexpr std::uint64_t kDot = 4; // master ticks per PPU dot (NTSC)
constexpr std::uint64_t kDotsPerLine = 341;
constexpr std::uint64_t kDotsPerFrame = 341 * 262;

} // namespace

TEST(Ppu, HorizontalMirroringFoldsSecondTable)
{
    PpuHarness h; // horizontal
    h.ppuWrite(0x2000, 0xAB);
    h.ppuWrite(0x2400, 0xCD); // horizontal: $2400 mirrors $2000
    EXPECT_EQ(h.ppuRead(0x2000), 0xCD); // same physical nametable
}

TEST(Ppu, VerticalMirroringKeepsTablesDistinct)
{
    PpuHarness h(/*vertical=*/true);
    h.ppuWrite(0x2000, 0xAB);
    h.ppuWrite(0x2400, 0xCD);
    EXPECT_EQ(h.ppuRead(0x2000), 0xAB);
    EXPECT_EQ(h.ppuRead(0x2400), 0xCD); // distinct under vertical mirroring
}

TEST(Ppu, PaletteSpriteZeroEntryMirrorsBackdrop)
{
    PpuHarness h;
    h.ppuWrite(0x3F00, 0x21);
    h.ppuWrite(0x3F10, 0x09);
    EXPECT_EQ(h.ppuRead(0x3F00), 0x09); // $3F10 aliases $3F00
    h.ppuWrite(0x3F04, 0x33);
    EXPECT_EQ(h.ppuRead(0x3F14), 0x33); // $3F14 aliases $3F04
}

TEST(Ppu, VblankFlagAndNmiAtLine241Dot1)
{
    PpuHarness h;
    h.scheduler.advanceMaster(29700 * 12); // clear the write-ignore window
    h.ppu.cpuWrite(0x2000, 0x80); // enable NMI
    EXPECT_FALSE(h.ppu.nmiLine());

    // Run THROUGH scanline 241, dot 1 of frame 2 — but past the 2-dot
    // read-suppression race window so the flag reads back set.
    const std::uint64_t targetTicks =
        (kDotsPerFrame + 241 * kDotsPerLine + 4) * kDot + kDot * 3; // +1 CPU: PPU lag
    h.scheduler.advanceMaster(targetTicks - h.scheduler.now());
    h.ppu.runTo(h.scheduler.now());
    EXPECT_NE(h.ppu.cpuRead(0x2002) & 0x80, 0);
    EXPECT_FALSE(h.ppu.nmiLine()); // the read just cleared the flag

    // Reading again still shows clear.
    EXPECT_EQ(h.ppu.cpuRead(0x2002) & 0x80, 0);
}

TEST(Ppu, FrameReadyWhenVisibleAreaCompletes)
{
    PpuHarness h;
    h.scheduler.advanceMaster(kDotsPerFrame * kDot + kDot * 3); // PPU lag
    h.ppu.runTo(h.scheduler.now());
    // Consume-once: the frame completed at scanline 239, dot 256.
    EXPECT_TRUE(h.ppu.takeFrameReady());
    EXPECT_FALSE(h.ppu.takeFrameReady());
}

TEST(Ppu, OddFrameDotSkipOnlyWhenRendering)
{
    PpuHarness rendering;
    rendering.scheduler.advanceMaster(29700 * 12);
    rendering.ppu.cpuWrite(0x2001, 0x08); // enable background
    // Two frames minus one dot (absolute): the odd frame's skipped dot lets
    // the PPU wrap to (0,0) one dot early.
    rendering.scheduler.advanceMaster(
        (2 * kDotsPerFrame - 1) * kDot + kDot * 3 - rendering.scheduler.now()); // +1 CPU: PPU lag
    rendering.ppu.runTo(rendering.scheduler.now());
    EXPECT_EQ(rendering.ppu.scanline(), 0);
    EXPECT_EQ(rendering.ppu.dot(), 0);

    PpuHarness blank; // rendering disabled: no skip
    blank.scheduler.advanceMaster((2 * kDotsPerFrame - 1) * kDot);
    blank.ppu.runTo(blank.scheduler.now());
    EXPECT_EQ(blank.ppu.scanline(), 261); // still one dot short of wrapping
}

// Colour emphasis (PPUMASK bits 5-7): each bit keeps its channel and
// attenuates the other two to 3/4; all three darken everything. Verified
// against the frozen kPalette by rendering one emphasized backdrop frame.
TEST(Ppu, ColourEmphasisAttenuatesOtherChannels)
{
    // Backdrop $21 (0x4240FF-ish row); render one full frame per mask.
    auto backdrop = [](std::uint8_t mask) {
        PpuHarness h;
        std::uint32_t frame[256 * 240] = {};
        h.ppu.setVideoOutput(frame);
        h.ppu.reset(true);
        h.scheduler.reset();
        h.scheduler.advanceMaster(29700 * 12); // clear ignore window
        h.ppuWrite(0x3F00, 0x21); // backdrop colour
        h.ppu.cpuWrite(0x2001, mask);
        for (int i = 0; i < 300; ++i) { // run past one frame
            h.scheduler.advanceMaster((341 * 262) * 4 - h.scheduler.now()
                % ((341 * 262) * 4));
            h.ppu.runTo(h.scheduler.now());
        }
        return frame[0];
    };
    const std::uint32_t base = backdrop(0x08); // bg on, no emphasis
    const std::uint32_t r = backdrop(0x28); // + red
    const std::uint32_t g = backdrop(0x48); // + green
    const std::uint32_t b = backdrop(0x88); // + blue
    const std::uint32_t all = backdrop(0xE8);
    auto px = [](std::uint32_t v, int shift) {
        return (v >> shift) & 0xFF;
    };
    // Red emphasis: R full, G/B at 3/4.
    EXPECT_EQ(px(r, 16), px(base, 16));
    EXPECT_EQ(px(r, 8), (px(base, 8) * 3) / 4);
    EXPECT_EQ(px(r, 0), (px(base, 0) * 3) / 4);
    // Green emphasis: G full, R/B at 3/4.
    EXPECT_EQ(px(g, 8), px(base, 8));
    EXPECT_EQ(px(g, 16), (px(base, 16) * 3) / 4);
    // Blue emphasis: B full, R/G at 3/4.
    EXPECT_EQ(px(b, 0), px(base, 0));
    EXPECT_EQ(px(b, 16), (px(base, 16) * 3) / 4);
    // All three: everything darkens.
    EXPECT_EQ(px(all, 16), (px(base, 16) * 3) / 4);
    EXPECT_EQ(px(all, 8), (px(base, 8) * 3) / 4);
}

namespace {

// Pixel 0 after two frames with rendering off, VRAM address `v`, backdrop
// $3F00 = `backdrop` and $3F05 = $16.
std::uint32_t idlePixel(std::uint16_t v, std::uint8_t backdrop)
{
    PpuHarness h;
    static std::uint32_t frame[256 * 240];
    h.ppu.setVideoOutput(frame);
    h.ppuWrite(0x3F00, backdrop);
    h.ppuWrite(0x3F05, 0x16);
    h.scheduler.advanceMaster(29700 * 12);
    h.ppu.cpuWrite(0x2001, 0x00); // rendering off
    h.ppu.cpuWrite(0x2006, static_cast<std::uint8_t>(v >> 8));
    h.ppu.cpuWrite(0x2006, static_cast<std::uint8_t>(v & 0xFF));
    h.scheduler.advanceMaster(2 * kDotsPerFrame * kDot);
    h.ppu.runTo(h.scheduler.now());
    return frame[0];
}

} // namespace

// nesdev "PPU palettes": with rendering off and v in $3F00-$3FFF, the PPU
// outputs the palette entry v points at instead of the backdrop.
TEST(Ppu, IdleOutputFollowsPaletteAddressInV)
{
    const std::uint32_t asBackdrop16 = idlePixel(0x3F00, 0x16);
    const std::uint32_t backdrop21 = idlePixel(0x3F00, 0x21);
    ASSERT_NE(asBackdrop16, backdrop21);
    EXPECT_EQ(idlePixel(0x3F05, 0x21), asBackdrop16); // entry $3F05 = $16
}

TEST(Ppu, IdleOutputUsesBackdropWhenVIsOutsidePaletteRam)
{
    EXPECT_EQ(idlePixel(0x2005, 0x21), idlePixel(0x3F00, 0x21));
}
