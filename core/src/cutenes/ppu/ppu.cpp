// 2C02 PPU implementation (tasks T021-T023, T026).
//
// Dot timeline (NTSC): scanlines 0-239 visible, 240 post-render, 241-260
// VBlank (flag set at 241,1; cleared at 261,1), 261 pre-render. 341 dots per
// scanline; odd frames skip one pre-render dot when rendering is enabled.
// All constants per research.md D2. The sprite-overflow diagonal-scan bug
// and one-dot $2002 read races arrive with US2 (tasks T034/T035).

#include "cutenes/ppu/ppu.hpp"

#include "cutenes/cartridge/cart.hpp"

namespace cutenes {
namespace {

constexpr int kVisibleLines = 240;
constexpr int kVblankLine = 241;
constexpr int kPreRenderLine = 261;

} // namespace

// Frozen default palette (contract: media-output.md — changing this table is
// a breaking change). Classic 2C02 NTSC master palette.
const std::array<std::uint32_t, 64> Ppu::kPalette = {
    0x666666, 0x002A88, 0x1412A7, 0x3B00A4, 0x5C007E, 0x6E0040, 0x6C0600,
    0x561D00, 0x333500, 0x0B4800, 0x005200, 0x004F08, 0x00404D, 0x000000,
    0x000000, 0x000000, 0xADADAD, 0x155FD9, 0x4240FF, 0x7527FE, 0xA01ACC,
    0xB71E7B, 0xB53120, 0x994E00, 0x6B6D00, 0x388700, 0x0C9300, 0x008F32,
    0x007C8D, 0x000000, 0x000000, 0x000000, 0xFFFEFF, 0x64B0FF, 0x9290FF,
    0xC676FF, 0xF36AFF, 0xFE6ECC, 0xFE8170, 0xEA9E22, 0xBCBE00, 0x88D800,
    0x5CE430, 0x45E082, 0x48CDDE, 0x4F4F4F, 0x000000, 0x000000, 0xFFFEFF,
    0xC0DFFF, 0xD3D2FF, 0xE8C8FF, 0xFBC2FF, 0xFEC4EA, 0xFECCC5, 0xF7D8A5,
    0xE4E594, 0xCFEF96, 0xBDF4AB, 0xB3F3CC, 0xB5EBF2, 0xB8B8B8, 0x000000,
    0x000000,
};

Ppu::Ppu(const RegionProfile& profile, Scheduler& scheduler)
    : m_profile(profile)
    , m_sched(scheduler)
{
}

void Ppu::attachCartridge(Cartridge* cartridge) { m_cart = cartridge; }
void Ppu::setVideoOutput(std::uint32_t* framebuffer) { m_frame = framebuffer; }

bool Ppu::renderingEnabled() const
{
    return (m_mask & 0x18) != 0;
}

void Ppu::reset(bool /*cold*/)
{
    m_ctrl = 0;
    m_mask = 0;
    m_status = 0;
    m_v = m_t = 0;
    m_fineX = m_w = 0;
    m_readBuffer = 0;
    m_oamAddr = 0;
    m_units.fill({});
    m_unitCount = 0;
    m_spriteZeroNext = m_spriteZeroActive = false;
    m_dots = 0;
    m_scanline = 0;
    m_dot = 0;
    m_oddFrame = false;
    m_suppressSet = false;
    m_nmiKilled = false;
    // Writes to $2000/$2001/$2005/$2006 are ignored for ~29658 CPU cycles
    // after reset (research.md D4).
    m_ignoreWritesUntil =
        m_sched.now() + 29658ULL * m_profile.cpuDivisor;
}

// --- Register file ---------------------------------------------------------

std::uint8_t Ppu::cpuRead(std::uint16_t addr)
{
    std::uint8_t value = m_decay;
    switch (addr) {
    case 0x2002:
        // Open-bus low 5 bits decay into the returned status byte.
        value = static_cast<std::uint8_t>((m_status & 0xE0) | (m_decay & 0x1F));
        // Race: reading at (or within a CPU clock of) the set dot returns
        // the flag clear and suppresses it for the rest of the frame
        // (nesdev PPU_registers; exercised by 02-vbl_set_time).
        if (m_scanline == kVblankLine && m_dot <= 2) {
            // Reading at the set dot: the flag reads clear; the phi2 sample
            // after this read sees the line low, so this cycle is covered.
            value &= static_cast<std::uint8_t>(~0x80);
        }
        if (m_scanline == kVblankLine && m_dot >= 1 && m_dot <= 4) {
            // Read within ~2 PPU clocks of the set: cancel this frame's NMI
            // even if a phi2 sample already latched the edge (rows 5-6).
            m_nmiKilled = true;
            m_status &= static_cast<std::uint8_t>(~0x80);
        }
        m_status &= static_cast<std::uint8_t>(~0x80); // reading clears VBlank
        m_w = 0;
        break;
    case 0x2004:
        value = m_oam[m_oamAddr];
        break;
    case 0x2007: {
        const std::uint16_t a = m_v & 0x3FFF;
        if (a >= 0x3F00) {
            value = paletteRead(static_cast<std::uint8_t>(a & 0x1F));
            m_readBuffer = m_vram[nametableIndex(a)];
        } else {
            value = m_readBuffer;
            m_readBuffer = ppuBusRead(a);
        }
        m_v = static_cast<std::uint16_t>(
            m_v + ((m_ctrl & 0x04) != 0 ? 32 : 1));
        break;
    }
    default:
        break; // $2000/$2001/$2003/$2005/$2006 are write-only
    }
    m_decay = value;
    return value;
}

void Ppu::cpuWrite(std::uint16_t addr, std::uint8_t value)
{
    m_decay = value;
    const bool writable = addr == 0x2002 || addr == 0x2003 || addr == 0x2004
        || addr == 0x2007 || m_sched.now() >= m_ignoreWritesUntil;
    if (!writable) {
        return; // the ~29658-cycle post-reset ignore window
    }
    switch (addr) {
    case 0x2000:
        m_ctrl = value;
        m_t = static_cast<std::uint16_t>((m_t & 0x73FF) | ((value & 0x03) << 10));
        break;
    case 0x2001:
        m_mask = value;
        break;
    case 0x2003:
        m_oamAddr = value;
        break;
    case 0x2004:
        m_oam[m_oamAddr] = value;
        ++m_oamAddr;
        break;
    case 0x2005:
        if (m_w == 0) {
            m_t = static_cast<std::uint16_t>((m_t & 0x7FE0) | (value >> 3));
            m_fineX = static_cast<std::uint8_t>(value & 0x07);
            m_w = 1;
        } else {
            m_t = static_cast<std::uint16_t>((m_t & 0x0C1F)
                | ((value & 0x07) << 12) | ((value & 0xF8) << 2));
            m_w = 0;
        }
        break;
    case 0x2006:
        if (m_w == 0) {
            m_t = static_cast<std::uint16_t>((m_t & 0x00FF) | ((value & 0x3F) << 8));
            m_w = 1;
        } else {
            m_t = static_cast<std::uint16_t>((m_t & 0x7F00) | value);
            m_v = m_t;
            m_w = 0;
        }
        break;
    case 0x2007: {
        const std::uint16_t a = m_v & 0x3FFF;
        if (a >= 0x3F00) {
            const std::uint8_t idx = static_cast<std::uint8_t>(a & 0x1F);
            m_palette[(idx & 3) == 0 ? static_cast<std::uint8_t>(idx & 0x0F) : idx] = value;
        } else {
            ppuBusWrite(a, value);
        }
        m_v = static_cast<std::uint16_t>(
            m_v + ((m_ctrl & 0x04) != 0 ? 32 : 1));
        break;
    }
    default:
        break;
    }
}

void Ppu::oamDmaWrite(std::uint8_t value)
{
    m_oam[m_oamAddr] = value;
    ++m_oamAddr;
}

// --- PPU bus ($0000-$3FFF) --------------------------------------------------

std::uint16_t Ppu::nametableIndex(std::uint16_t addr) const
{
    // $2000-$2FFF: 1KB logical nametable, mirrored per the cartridge.
    const std::uint16_t offset = addr & 0x0FFF;
    std::uint16_t table = offset >> 10;
    if (m_cart != nullptr) {
        switch (m_cart->mirroring()) {
        case Mirroring::Vertical: // NT A at $2000, B at $2400 (tables 0,2 / 1,3)
            table &= 1;
            break;
        case Mirroring::Horizontal: // A,A,B,B
            table >>= 1;
            break;
        case Mirroring::FourScreen:
            break; // all four distinct (requires extra VRAM; mapper support)
        case Mirroring::MapperControlled:
            break;
        }
    }
    return static_cast<std::uint16_t>(table * 1024 + (offset & 0x3FF));
}

std::uint8_t Ppu::ppuBusRead(std::uint16_t addr)
{
    addr &= 0x3FFF;
    if (addr < 0x2000) {
        return m_cart != nullptr ? m_cart->chrRead(addr) : 0;
    }
    if (addr < 0x3F00) {
        return m_vram[nametableIndex(addr)];
    }
    return paletteRead(static_cast<std::uint8_t>(addr & 0x1F));
}

void Ppu::ppuBusWrite(std::uint16_t addr, std::uint8_t value)
{
    addr &= 0x3FFF;
    if (addr < 0x2000) {
        if (m_cart != nullptr) {
            m_cart->chrWrite(addr, value); // CHR-RAM only
        }
    } else if (addr < 0x3F00) {
        m_vram[nametableIndex(addr)] = value;
    }
}

std::uint8_t Ppu::paletteRead(std::uint8_t index) const
{
    if ((index & 3) == 0) {
        index &= 0x0F; // $3F10/14/18/1C mirror $3F00/04/08/0C
    }
    return m_palette[index];
}

// --- Scroll updates -----------------------------------------------------------

void Ppu::incrementX()
{
    if ((m_v & 0x001F) == 31) {
        m_v = static_cast<std::uint16_t>((m_v & ~0x001F) ^ 0x0400);
    } else {
        ++m_v;
    }
}

void Ppu::incrementY()
{
    if ((m_v & 0x7000) != 0x7000) {
        m_v += 0x1000;
    } else {
        m_v &= 0x0FFF;
        std::uint16_t y = (m_v & 0x03E0) >> 5;
        if (y == 29) {
            y = 0;
            m_v ^= 0x0800;
        } else if (y == 31) {
            y = 0;
        } else {
            ++y;
        }
        m_v = static_cast<std::uint16_t>((m_v & ~0x03E0) | (y << 5));
    }
}

void Ppu::copyHorizontal()
{
    m_v = static_cast<std::uint16_t>((m_v & ~0x041F) | (m_t & 0x041F));
}

void Ppu::copyVertical()
{
    m_v = static_cast<std::uint16_t>((m_v & ~0x7BE0) | (m_t & 0x7BE0));
}

void Ppu::setBackgroundShifters()
{
    m_shiftLo = static_cast<std::uint16_t>((m_shiftLo & 0xFF00) | m_latchLo);
    m_shiftHi = static_cast<std::uint16_t>((m_shiftHi & 0xFF00) | m_latchHi);
    const std::uint8_t shift = m_latchAT & 3;
    m_shiftAttrLo = static_cast<std::uint16_t>(
        (m_shiftAttrLo & 0xFF00) | ((shift & 1) ? 0xFF : 0x00));
    m_shiftAttrHi = static_cast<std::uint16_t>(
        (m_shiftAttrHi & 0xFF00) | ((shift & 2) ? 0xFF : 0x00));
}

// Background fetch cycles: dots 1-257 and 321-336, 8-dot groups.
void Ppu::fetchCycle(std::uint32_t dot)
{
    if (!renderingEnabled()) {
        return;
    }
    if ((dot >= 1 && dot <= 256) || (dot >= 321 && dot <= 336)) {
        // Shift the pipeline every dot; reload at each group start.
        m_shiftLo = static_cast<std::uint16_t>(m_shiftLo << 1);
        m_shiftHi = static_cast<std::uint16_t>(m_shiftHi << 1);
        m_shiftAttrLo = static_cast<std::uint16_t>(m_shiftAttrLo << 1);
        m_shiftAttrHi = static_cast<std::uint16_t>(m_shiftAttrHi << 1);
        if ((dot >= 2 && dot <= 257) || (dot >= 322 && dot <= 337)) {
            switch (dot & 7) {
            case 2: // NT
                m_latchNT = m_vram[nametableIndex(
                    static_cast<std::uint16_t>(0x2000 | (m_v & 0x0FFF)))];
                break;
            case 4: { // AT
                const std::uint16_t base =
                    static_cast<std::uint16_t>(0x23C0 | (m_v & 0x0C00));
                const std::uint8_t at = m_vram[nametableIndex(static_cast<std::uint16_t>(
                    base | ((m_v >> 4) & 0x38) | ((m_v >> 2) & 0x07)))];
                m_latchAT = static_cast<std::uint8_t>(
                    (at >> (((m_v >> 4) & 4) | (m_v & 2))) & 3);
                break;
            }
            case 6: { // pattern low
                const std::uint16_t addr = static_cast<std::uint16_t>(
                    ((m_ctrl & 0x10) != 0 ? 0x1000 : 0)
                    + m_latchNT * 16 + ((m_v >> 12) & 7));
                m_latchLo = ppuBusRead(addr);
                break;
            }
            case 0: { // pattern high + horizontal advance
                const std::uint16_t addr = static_cast<std::uint16_t>(
                    ((m_ctrl & 0x10) != 0 ? 0x1000 : 0)
                    + m_latchNT * 16 + ((m_v >> 12) & 7) + 8);
                m_latchHi = ppuBusRead(addr);
                incrementX();
                break;
            }
            case 1:
                setBackgroundShifters();
                break;
            default:
                break;
            }
        }
    }
}

// --- Sprites -------------------------------------------------------------------

void Ppu::evaluateSprites()
{
    // Select up to 8 in-range sprites for the NEXT scanline. The buggy
    // 9th-sprite diagonal scan (m-increment) is US2 scope (task T035).
    const int line = m_scanline + 1;
    const int height = (m_ctrl & 0x20) != 0 ? 16 : 8;
    std::size_t out = 0;
    bool zeroFound = false;
    m_spriteZeroNext = false;
    for (std::size_t i = 0; i < 64; ++i) {
        const int y = m_oam[i * 4];
        if (line >= y && line < y + height) {
            if (i == 0) {
                zeroFound = true;
            }
            if (out < 8) {
                m_oam2[out * 4 + 0] = m_oam[i * 4 + 0];
                m_oam2[out * 4 + 1] = m_oam[i * 4 + 1];
                m_oam2[out * 4 + 2] = m_oam[i * 4 + 2];
                m_oam2[out * 4 + 3] = m_oam[i * 4 + 3];
                if (zeroFound && i == 0) {
                    m_spriteZeroNext = true;
                }
                ++out;
            } else {
                m_status |= 0x20; // overflow (9th in-range sprite)
                break;
            }
        }
    }
    for (; out < 8; ++out) {
        m_oam2[out * 4 + 0] = 0xFF;
        m_oam2[out * 4 + 1] = 0xFF;
        m_oam2[out * 4 + 2] = 0xFF;
        m_oam2[out * 4 + 3] = 0xFF;
    }
}

void Ppu::loadSpriteUnits()
{
    if (!renderingEnabled()) {
        m_unitCount = 0;
        m_spriteZeroActive = false;
        return;
    }
    const int line = m_scanline + 1;
    const bool tall = (m_ctrl & 0x20) != 0;
    const bool spritePatternLeft = (m_ctrl & 0x08) != 0;
    m_unitCount = 0;
    m_spriteZeroActive = m_spriteZeroNext;
    for (int i = 0; i < 8; ++i) {
        const std::uint8_t y = m_oam2[static_cast<std::size_t>(i) * 4 + 0];
        const std::uint8_t tile = m_oam2[static_cast<std::size_t>(i) * 4 + 1];
        const std::uint8_t attr = m_oam2[static_cast<std::size_t>(i) * 4 + 2];
        int row = line - y - 1;
        if (row < 0 || row >= (tall ? 16 : 8)) {
            continue; // empty slot
        }
        if ((attr & 0x80) != 0) {
            row = (tall ? 15 : 7) - row; // vertical flip
        }
        std::uint16_t addr;
        if (tall) {
            addr = static_cast<std::uint16_t>(
                (tile & 1) * 0x1000 + (tile & 0xFE) * 16 + (row & 8) * 2
                + (row & 7));
        } else {
            addr = static_cast<std::uint16_t>(
                (spritePatternLeft ? 0x1000 : 0) + tile * 16 + row);
        }
        Unit& unit = m_units[static_cast<std::size_t>(m_unitCount)];
        unit.x = m_oam2[static_cast<std::size_t>(i) * 4 + 3];
        unit.attr = attr;
        unit.lo = ppuBusRead(addr);
        unit.hi = ppuBusRead(static_cast<std::uint16_t>(addr + 8));
        ++m_unitCount;
    }
}

void Ppu::renderPixel(std::uint32_t x)
{
    std::uint8_t index = 0; // final palette index (0 = backdrop)
    if (renderingEnabled()) {
        const bool showBg = (m_mask & 0x08) != 0 && (x >= 8 || (m_mask & 0x02) != 0);
        const bool showSprites =
            (m_mask & 0x10) != 0 && (x >= 8 || (m_mask & 0x04) != 0);

        std::uint8_t bgPixel = 0, bgPalette = 0;
        if (showBg) {
            const std::uint8_t bit = static_cast<std::uint8_t>(15 - m_fineX);
            bgPixel = static_cast<std::uint8_t>(
                (((m_shiftHi >> bit) & 1) << 1) | ((m_shiftLo >> bit) & 1));
            bgPalette = static_cast<std::uint8_t>(
                (((m_shiftAttrHi >> bit) & 1) << 1) | ((m_shiftAttrLo >> bit) & 1));
        }

        std::uint8_t spPixel = 0, spPalette = 0, spPriority = 0;
        bool isSpriteZero = false;
        if (showSprites) {
            for (int i = 0; i < m_unitCount; ++i) {
                const Unit& unit = m_units[static_cast<std::size_t>(i)];
                const int offset = static_cast<int>(x) - unit.x;
                if (offset < 0 || offset > 7) {
                    continue;
                }
                const int bit = (unit.attr & 0x40) != 0 ? offset : 7 - offset;
                const std::uint8_t p = static_cast<std::uint8_t>(
                    (((unit.hi >> bit) & 1) << 1) | ((unit.lo >> bit) & 1));
                if (p == 0) {
                    continue; // transparent
                }
                spPixel = p;
                spPalette = static_cast<std::uint8_t>(unit.attr & 3);
                spPriority = unit.attr & 0x20;
                isSpriteZero = (i == 0) && m_spriteZeroActive;
                break; // lowest index wins
            }
        }

        // Sprite-0 hit: both layers opaque, exclusions per research.md D3.
        if (isSpriteZero && bgPixel != 0 && x != 255) {
            m_status |= 0x40;
        }

        if (spPixel != 0 && (bgPixel == 0 || spPriority == 0)) {
            index = static_cast<std::uint8_t>(0x10 | (spPalette << 2) | spPixel);
        } else if (bgPixel != 0) {
            index = static_cast<std::uint8_t>((bgPalette << 2) | bgPixel);
        }
    }

    std::uint8_t color = paletteRead(index);
    if ((m_mask & 0x01) != 0) { // grayscale
        color &= 0x30;
    }
    if (m_frame != nullptr) {
        m_frame[static_cast<std::size_t>(m_scanline) * 256 + x] = kPalette[color & 0x3F];
    }
}

// --- Dot timeline ---------------------------------------------------------------

void Ppu::stepDot()
{
    // Pre-render line uses index 261; wrap dot/scanline with the odd-frame
    // dot skip when rendering.
    if (m_scanline == kPreRenderLine && m_dot == 339 && m_oddFrame
        && renderingEnabled()) {
        m_dot = 340; // skip the final idle dot of the odd frame
    }

    if (m_scanline < kVisibleLines) {
        if (m_dot >= 1 && m_dot <= 256) {
            renderPixel(static_cast<std::uint32_t>(m_dot - 1));
        }
        fetchCycle(static_cast<std::uint32_t>(m_dot));
        if (m_dot == 256) {
            incrementY();
        } else if (m_dot == 257) {
            copyHorizontal();
            loadSpriteUnits();
        } else if (m_dot == 65) {
            evaluateSprites();
        }
    } else if (m_scanline == kPreRenderLine) {
        fetchCycle(static_cast<std::uint32_t>(m_dot));
        if (m_dot == 256) {
            incrementY();
        } else if (m_dot == 257) {
            copyHorizontal();
            evaluateSpritesForLine0();
        } else if (m_dot >= 280 && m_dot <= 304) {
            copyVertical();
        }
    }

    if (m_scanline == kVisibleLines - 1 && m_dot == 256) {
        m_frameComplete = true; // final visible pixel emitted (239,255)
    }
    if (m_scanline == kVblankLine && m_dot == 1) {
        m_status |= 0x80; // NMI line rises; the CPU samples at phi2
    }
    if (m_scanline == kPreRenderLine && m_dot == 1) {
        m_status &= static_cast<std::uint8_t>(~0xE0); // clear V/S0/O
        m_suppressSet = false;
    m_nmiKilled = false;
        m_nmiKilled = false;
        m_oddFrame = !m_oddFrame;
    }

    ++m_dot;
    if (m_dot > 340) {
        m_dot = 0;
        ++m_scanline;
        if (m_scanline > kPreRenderLine) {
            m_scanline = 0;
        }
    }
}

void Ppu::evaluateSpritesForLine0()
{
    // Hardware quirk: evaluation does not run on the pre-render line, so
    // scanline 0 never shows sprites (secondary OAM stays cleared).
    m_oam2.fill(0xFF);
    m_spriteZeroNext = false;
}

void Ppu::runTo(std::uint64_t masterClock)
{
    const std::uint64_t targetDots = masterClock / m_profile.ppuDivisor;
    while (m_dots < targetDots) {
        stepDot();
        ++m_dots;
    }
}

bool Ppu::takeFrameReady()
{
    // Consume-once: set when the final visible pixel is emitted, cleared by
    // the machine's per-frame poll.
    if (m_frameComplete) {
        m_frameComplete = false;
        return true;
    }
    return false;
}

} // namespace cutenes
