// 2C02 PPU implementation (tasks T021-T023, T026).
//
// Dot timeline (NTSC): scanlines 0-239 visible, 240 post-render, 241-260
// VBlank (flag set at 241,1; cleared at 261,1), 261 pre-render. 341 dots per
// scanline; odd frames skip one pre-render dot when rendering is enabled.
// All constants per research.md D2. The sprite-overflow diagonal-scan bug
// and one-dot $2002 read races arrive with US2 (tasks T034/T035).

#include "cutenes/ppu/ppu.hpp"


#include "cutenes/serialize/save_state.hpp"
#include <cstdio>
#include <cstdlib>
#include "cutenes/system/timing.hpp"

#include "cutenes/cartridge/cart.hpp"

namespace cutenes {
namespace {

constexpr int kVisibleLines = 240;
// The dot the eight sprite PATTERN fetches begin (the 8-dot groups run
// garbage-NT, garbage-AT, pattern-lo, pattern-hi; the first CHR access of
// the sprite window is the first pattern pair). Calibrated so the mode-$08
// (sprites at $1000) counter clock lands at pre-render dot 260 — the
// razor edge of blargg 4-scanline_timing subtests #2/#3.
constexpr int kSpriteFetchDot = 260;
constexpr int kVblankLine = 241;

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
    // ~0.5 s of master clock before the open-bus latch has bled to zero.
    m_decayTicks = m_profile.masterClockNum / m_profile.masterClockDen / 2;
    buildEmphasisTables();
}

// PPUMASK bits 5-7 (colour emphasis): each bit boosts one channel and
// attenuates the other two; all three set darken everything (nesdev
// "Colour emphasis"). Modeled on the frozen base palette with a 3/4
// attenuation — deterministic integer math, byte-identical everywhere.
// $0F black (base 0x000000) is naturally unaffected, as on hardware.
// Greyscale still gets tinted: emphasis applies to the looked-up RGB.
// PAL PPUs swap the red and green bit meanings (nesdev).
void Ppu::buildEmphasisTables()
{
    // NTSC: bit 5 = red, bit 6 = green, bit 7 = blue.
    int redSlot = 0, greenSlot = 1;
    if (m_profile.ppuDivisor == regionProfile(Region::Pal).ppuDivisor) {
        redSlot = 1; // PAL swaps red/green emphasis
        greenSlot = 0;
    }
    for (int e = 0; e < 8; ++e) {
        const bool all = (e & 7) == 7;
        const bool r = (e & (1 << redSlot)) != 0;
        const bool g = (e & (1 << greenSlot)) != 0;
        const bool b = (e & 0x04) != 0; // bit 7 -> slot 2
        for (int i = 0; i < 64; ++i) {
            const std::uint32_t rgb = kPalette[static_cast<std::size_t>(i)];
            const auto scale = [all](int v, bool emphasized) {
                return (all || !emphasized) ? (v * 3) / 4 : v;
            };
            const int rr = scale((rgb >> 16) & 0xFF, r);
            const int gg = scale((rgb >> 8) & 0xFF, g);
            const int bb = scale((rgb >> 0) & 0xFF, b);
            m_emphasis[static_cast<std::size_t>(e)]
                      [static_cast<std::size_t>(i)] =
                static_cast<std::uint32_t>((rr << 16) | (gg << 8) | bb);
        }
    }
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
        m_sched.now() + timing::kPostResetWriteIgnoreCpuCycles * m_profile.cpuDivisor;
}

// --- Register file ---------------------------------------------------------

// The open-bus latch after RC decay: a bit reads back zero once its last
// charge is stale (ppu_open_bus #3).
std::uint8_t Ppu::decayedBusValue() const
{
    const std::uint64_t now = m_sched.now();
    std::uint8_t value = 0;
    for (int bit = 0; bit < 8; ++bit) {
        const std::uint8_t mask = static_cast<std::uint8_t>(1u << bit);
        if ((m_decay & mask) != 0 && now - m_decayMaster[bit] < m_decayTicks) {
            value |= mask;
        }
    }
    return value;
}

void Ppu::chargeBusBits(std::uint8_t mask, std::uint8_t value)
{
    const std::uint64_t now = m_sched.now();
    m_decay = static_cast<std::uint8_t>(
        (m_decay & ~mask) | (value & mask));
    for (int bit = 0; bit < 8; ++bit) {
        if ((mask & (1u << bit)) != 0) {
            m_decayMaster[bit] = now;
        }
    }
}

std::uint8_t Ppu::cpuRead(std::uint16_t addr)
{
    std::uint8_t value = decayedBusValue();
    switch (addr) {
    case 0x2002:
        if (std::getenv("RDDBG")) std::fprintf(stderr, "RD2002 master=%llu line=%d dot=%d lag=%02X st=%02X\n",
            (unsigned long long)m_sched.now(), m_scanline, m_dot, m_statusLag, m_status);
        // Open-bus low 5 bits decay into the returned status byte.
        value = static_cast<std::uint8_t>(
            (m_statusLag & 0xE0) | (decayedBusValue() & 0x1F));
        // Race: reading at (or within a CPU clock of) the set dot returns
        // the flag clear and suppresses it for the rest of the frame
        // (nesdev PPU_registers; exercised by 02-vbl_set_time).
        if (m_scanline == kVblankLine && m_dot <= timing::kVblRaceDotMax) {
            // Reading at the set dot: the flag reads clear; the phi2 sample
            // after this read sees the line low, so this cycle is covered.
            value &= static_cast<std::uint8_t>(~0x80);
        } else if ((m_status & 0x80) != 0 && (m_statusLag & 0x80) == 0
            && m_scanline == kVblankLine) {
            // Asymmetric lag (T037 session 25): the CLEAR transition reads
            // back set through the end of its dot (03-vbl_clear_time), but
            // a SET that landed earlier in THIS dot is already visible —
            // the hardware flag rises mid-dot and a later phi2 sample in
            // the same dot reads it (the sprdma boot poll: one fewer 7-
            // cycle iteration per blargg's validated alignment).
            value |= 0x80;
        }
        if (m_scanline == kVblankLine && m_dot >= timing::kNmiKillDotMin && m_dot <= timing::kNmiKillDotMax) {
            // Read within ~2 PPU clocks of the set: cancel this frame's NMI
            // even if a phi2 sample already latched the edge (rows 5-6).
            m_nmiKilled = true;
            m_status &= static_cast<std::uint8_t>(~0x80);
        }
        m_status &= static_cast<std::uint8_t>(~0x80); // reading clears VBlank
        m_statusLag = m_status; // a read's side effects are visible to the next read
        m_w = 0;
        break;
    case 0x2004:
        value = m_oam[m_oamAddr];
        if (renderingEnabled() && m_scanline < kVisibleLines
            && m_dot >= 1 && m_dot <= 64) {
            value = 0xFF; // secondary-OAM clear phase reads $FF
        } else if ((m_oamAddr & 3) == 2) {
            // Attribute bytes: bits 2-4 are unimplemented in the OAM
            // DRAM and read back as 0 (nesdev PPU OAM; blargg oam_stress).
            value = static_cast<std::uint8_t>(value & 0xE3);
        }
        break;
    case 0x2007: {
        const std::uint16_t a = m_v & 0x3FFF;
        if (a >= 0x3F00) {
            // Palette reads are immediate and drive only bits 5-0; bits
            // 7-6 stay open bus (ppu_open_bus #8; nesdev PPU registers).
            value = static_cast<std::uint8_t>(
                (paletteRead(static_cast<std::uint8_t>(a & 0x1F)) & 0x3F)
                | (decayedBusValue() & 0xC0));
            m_readBuffer = readNametable(static_cast<std::uint16_t>(
                0x2000 | (a & 0x0FFF)));
        } else {
            value = m_readBuffer;
            // Double-read quirk (blargg double_2007_read): a $2007 read
            // on the cycle immediately after another $2007 read latches
            // the SAME value the first read latched — the refill from the
            // first access has not landed in the visible latch yet. The
            // refill and v increment still occur for both accesses.
            if (m_sched.now() == m_lastVramReadMaster + 12) {
                value = m_lastVramReadValue;
            }
            m_lastVramReadMaster = m_sched.now();
            m_lastVramReadValue = value;
            m_readBuffer = ppuBusRead(a);
        }
        clockIfA12Rose();
        break;
    }
    default:
        break; // $2000/$2001/$2003/$2005/$2006 are write-only
    }
    // Only lines the register actually drives get recharged: $2002 drives
    // bits 7-5 alone (the low 5 keep decaying, #7), a palette $2007 read
    // drives bits 5-0 (#8), $2004 and normal $2007 drive all 8, and reads
    // of write-only registers drive nothing (#5).
    if (addr == 0x2002) {
        chargeBusBits(0xE0, value);
    } else if (addr == 0x2004) {
        chargeBusBits(0xFF, value);
    } else if (addr == 0x2007) {
        chargeBusBits((m_v & 0x3FFF) >= 0x3F00 ? 0x3F : 0xFF, value);
    }
    return value;
}

void Ppu::cpuWrite(std::uint16_t addr, std::uint8_t value)
{
    chargeBusBits(0xFF, value);
    const bool writable = addr == 0x2002 || addr == 0x2003 || addr == 0x2004
        || addr == 0x2007 || m_sched.now() >= m_ignoreWritesUntil;
    if (!writable) {
        return; // the ~29658-cycle post-reset ignore window
    }
    switch (addr) {
    case 0x2000:
        m_ctrl = value;
        m_t = static_cast<std::uint16_t>((m_t & 0x73FF) | ((value & 0x03) << 10));
        if (m_cart != nullptr) {
            m_cart->setPpuCtrl(value);
        }
        break;
    case 0x2001:
        m_mask = value;
        if (m_cart != nullptr) {
            m_cart->setPpuMask(value);
        }
        break;
    case 0x2003:
        m_oamAddr = value;
        break;
    case 0x2004:
        if ((m_oamAddr & 3) == 2) {
            // Attribute bytes: bits 2-4 have no DRAM cells (nesdev PPU
            // OAM) — writes drop them so reads and save states match.
            value = static_cast<std::uint8_t>(value & 0xE3);
        }
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
            // With rendering off, v drives the PPU address bus, so the
            // write itself is a bus event (blargg 3-A12_clocking #4: a
            // $2006 write clocks the counter when bit 12 rises). While
            // rendering, the fetch bus owns A12 and the CPU write is not
            // visible to the mapper.
            if (!renderingEnabled()) {
                notifyA12(m_v);
            }
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
        clockIfA12Rose();
        break;
    }
    default:
        break;
    }
}

void Ppu::clockIfA12Rose()
{
    const std::uint16_t next = static_cast<std::uint16_t>(
        m_v + ((m_ctrl & 0x04) != 0 ? 32 : 1));
    m_v = next;
    // Only when the CPU owns the bus (rendering off) does the $2007
    // access's post-increment address reach the mapper's A12 input.
    if (!renderingEnabled()) {
        notifyA12(m_v);
    }
}

// Fetch-level A12 for the MMC3 scanline counter (Mesen's A12Watcher
// model): every PATTERN-fetch address change (and every CPU $2006/$2007
// access) is reported; a rise clocks the mapper's IRQ counter only if a
// LOW fetch was seen first and at least ~1 PPU dot passed since the fall
// was stamped. Nametable/attribute fetches do not touch the reported bus,
// so a background at $1000 never edges against its own NT gaps.
void Ppu::notifyA12(std::uint16_t addr)
{
    if ((addr & 0x1000) != 0) {
        const bool rise = mA12LowMaster != 0
            && m_sched.now() - mA12LowMaster >= 3;
        mA12LowMaster = 0;
        if (rise && m_cart != nullptr) {
            m_cart->clockIrq();
        }
    } else if (mA12LowMaster == 0) {
        mA12LowMaster = m_sched.now();
    }
}

void Ppu::oamDmaWrite(std::uint8_t value)
{
    if ((m_oamAddr & 3) == 2) {
        // Attribute bytes: bits 2-4 have no DRAM cells (nesdev PPU OAM).
        value = static_cast<std::uint8_t>(value & 0xE3);
    }
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

// Nametable access with mapper-controlled mapping (MMC5 $5105): each 1K
// slot can point at CIRAM page 0/1, the mapper's ExRAM, or fill mode.
std::uint8_t Ppu::readNametable(std::uint16_t addr) const
{
    if (m_cart != nullptr) {
        switch (m_cart->ntSource(addr)) {
        case Mapper::NtSource::Ciram0:
            return m_vram[addr & 0x3FF];
        case Mapper::NtSource::Ciram1:
            return m_vram[0x400 + (addr & 0x3FF)];
        case Mapper::NtSource::Mapper:
            return m_cart->ntRead(addr);
        case Mapper::NtSource::Default:
            break;
        }
    }
    return m_vram[nametableIndex(addr)];
}

void Ppu::writeNametable(std::uint16_t addr, std::uint8_t value)
{
    if (m_cart != nullptr) {
        switch (m_cart->ntSource(addr)) {
        case Mapper::NtSource::Ciram0:
            m_vram[addr & 0x3FF] = value;
            return;
        case Mapper::NtSource::Ciram1:
            m_vram[0x400 + (addr & 0x3FF)] = value;
            return;
        case Mapper::NtSource::Mapper:
            m_cart->ntWrite(addr, value);
            return;
        case Mapper::NtSource::Default:
            break;
        }
    }
    m_vram[nametableIndex(addr)] = value;
}

int Ppu::fetchColumn(std::uint32_t dot) const
{
    // 34 background fetches per line: dots 321-336 prefetch columns 0-1 for
    // the next line, dots 1-256 fetch columns 2-33.
    if (dot >= 321) {
        return static_cast<int>((dot - 321) / 8);
    }
    return 2 + static_cast<int>((dot - 1) / 8);
}

int Ppu::fetchScanline() const
{
    // Pre-render fetches build line 0; end-of-line prefetches build the
    // next visible line.
    if (m_scanline == preRenderLine()) {
        return 0;
    }
    return m_dot >= 321 ? m_scanline + 1 : m_scanline;
}

std::uint8_t Ppu::ppuBusRead(std::uint16_t addr) const
{
    addr &= 0x3FFF;
    if (addr < 0x2000) {
        return m_cart != nullptr ? m_cart->chrRead(addr) : 0;
    }
    if (addr < 0x3F00) {
        return readNametable(addr);
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
        writeNametable(addr, value);
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
            case 2: { // NT
                m_latchNT = readNametable(static_cast<std::uint16_t>(
                    0x2000 | (m_v & 0x0FFF)));
                break;
            }
            case 4: {
                // The pattern-low ADDRESS hits the bus at the first dot of
                // its fetch pair (the read completes two dots later); A12
                // must be notified there. Calibration: the mode-$10
                // prefetch clock lands at dot 324 (nesdev MMC3: "cycle
                // 324 of the previous scanline") and the mode-$10 first
                // clock exactly 256 dots before the mode-$08 sprite fetch
                // (blargg 4-scanline_timing's scanline_0_10 constant).
                notifyA12(static_cast<std::uint16_t>(
                    ((m_ctrl & 0x10) != 0 ? 0x1000 : 0)
                    + m_latchNT * 16 + ((m_v >> 12) & 7)));
                // AT
                const std::uint16_t base =
                    static_cast<std::uint16_t>(0x23C0 | (m_v & 0x0C00));
                const std::uint16_t atAddr = static_cast<std::uint16_t>(
                    base | ((m_v >> 4) & 0x38) | ((m_v >> 2) & 0x07));
                std::uint8_t at = readNametable(atAddr);
                if (m_cart != nullptr) {
                    // MMC5 extended attributes / vertical split supply the
                    // raw attribute byte from ExRAM.
                    Mapper::BgFetch fetch;
                    fetch.tileColumn = fetchColumn(dot);
                    fetch.scanline = fetchScanline();
                    fetch.v = m_v;
                    fetch.mask = m_mask;
                    fetch.attribute = true;
                    if (m_cart->bgFetch(fetch)) {
                        at = fetch.value;
                    }
                }
                m_latchAT = static_cast<std::uint8_t>(
                    (at >> (((m_v >> 4) & 4) | (m_v & 2))) & 3);
                break;
            }
            case 6: {
                std::uint32_t addr = static_cast<std::uint16_t>(
                    ((m_ctrl & 0x10) != 0 ? 0x1000 : 0)
                    + m_latchNT * 16 + ((m_v >> 12) & 7));
                bool direct = false;
                if (m_cart != nullptr) {
                    Mapper::BgFetch fetch;
                    fetch.tileColumn = fetchColumn(dot);
                    fetch.scanline = fetchScanline();
                    fetch.v = m_v;
                    fetch.mask = m_mask;
                    fetch.pattern = true;
                    fetch.addr = addr;
                    if (m_cart->bgFetch(fetch)) {
                        addr = fetch.addr;
                        direct = true;
                    }
                }
                notifyA12(static_cast<std::uint16_t>(addr));
                // pattern low
                // PPUCTRL bit 4 selects the BACKGROUND pattern table,
                // bit 3 the 8x8 SPRITE table (nesdev PPU_registers layout
                // "VPHB SINN": B = bit 4, S = bit 3; Mesen maps them the
                // same way, and blargg's own comment on PPUCTRL $08 says
                // "sprites use tiles at $1xxx").
                m_latchLo = direct ? m_cart->chrReadRaw(addr)
                                   : ppuBusRead(static_cast<std::uint16_t>(addr));
                break;
            }
            case 0: { // pattern high + horizontal advance
                std::uint32_t addr = static_cast<std::uint16_t>(
                    ((m_ctrl & 0x10) != 0 ? 0x1000 : 0)
                    + m_latchNT * 16 + ((m_v >> 12) & 7) + 8);
                bool direct = false;
                if (m_cart != nullptr) {
                    Mapper::BgFetch fetch;
                    fetch.tileColumn = fetchColumn(dot);
                    fetch.scanline = fetchScanline();
                    fetch.v = m_v;
                    fetch.mask = m_mask;
                    fetch.pattern = true;
                    fetch.addr = addr;
                    if (m_cart->bgFetch(fetch)) {
                        addr = fetch.addr;
                        direct = true;
                    }
                }
                m_latchHi = direct ? m_cart->chrReadRaw(addr)
                                   : ppuBusRead(static_cast<std::uint16_t>(addr));
                // Reload the shifter's low byte at the END of the fetch
                // group (dot 8k, after this dot's shift): 8 more shifts
                // put the byte in the sampling half exactly when its
                // pixels begin. Reloading at dot 8k+1 delayed every tile
                // by one pixel (sprite_hit 02/03/04: the bg's left edge
                // rendered one pixel late).
                setBackgroundShifters();
                incrementX();
                break;
            }
            default:
                break;
            }
        }
    }
}

// --- Sprites -------------------------------------------------------------------

void Ppu::evalDot(std::uint64_t dot)
{
    // Per-dot sprite evaluation, visible scanlines, dots 1-256, rendering
    // enabled — the literal PPU_sprite_evaluation algorithm (research.md
    // D3, task T035). Odd dots read primary OAM at OAMADDR; even dots
    // advance the machine. OAMADDR IS the evaluation pointer (byte offset
    // 4n+m), so $2003 writes mid-scanline land in the scan's path.
    // The comparison uses THIS scanline (the wiki: y in [scanline,
    // scanline+7]); loadSpriteUnits then renders row = scanline - y on
    // the NEXT line — using scanline+1 here cut every sprite's bottom
    // row and shifted evaluation one line late (sprite_hit 03/07/08).
    const int line = m_scanline;
    const int height = (m_ctrl & 0x20) != 0 ? 16 : 8;
    const bool odd = (dot & 1) != 0;

    if (dot <= 64) {
        // Secondary-OAM clear: normal read/write cycles with reads forced
        // to $FF. The clear IS the per-line reset of the evaluation
        // machine (hardware resets with the secondary buffer) — including
        // the overflow-scan state, which otherwise leaks across vblank
        // (no visible-line dot 0 there) into frames that enable rendering
        // late (sprite_overflow 2.Details #9 false positive).
        if (dot == 1) {
            m_evalStep = 0;
            m_secCount = 0;
            m_secByte = 0;
            m_ovM = 0;
            m_evalOverflow = false;
            m_secSecondaryRead = false;
            m_evalDone = false;
            // Sprite-zero candidacy is decided fresh each line: leaving a
            // stale latch made the FIRST in-range sprite of a later table
            // count as sprite 0 (ppu_read_buffer's no-hit DMA halves all
            // reported spurious hits).
            m_spriteZeroNext = false;
        }
        // The clear's 0-31 counter is INTERNAL (dot-derived); the
        // evaluation pointer must still be OAMADDR at dot 65 — the wiki's
        // algorithm starts at n=0, and the 2C02G/H refresh bug fires
        // relative to OAMADDR at evaluation START (advancing m_oamAddr
        // here shifted the whole scan by 8 sprites: sprite 0 was only
        // evaluated on lines where the wrap reached it, killing sprite-0
        // hit; blargg sprite_hit 01 #2).
        if (odd) {
            m_evalRead = 0xFF;
        } else {
            m_oam2[static_cast<std::size_t>((dot / 2) - 1)] = 0xFF;
        }
        return;
    }

    if (dot == 65 && (m_oamAddr & 0xF8) != 0) {
        // 2C02G/H refresh bug: nonzero OAMADDR at evaluation start copies
        // the 8 bytes at OAMADDR & $F8 over the first 8 bytes of OAM.
        const std::uint8_t src = static_cast<std::uint8_t>(m_oamAddr & 0xF8);
        for (int i = 0; i < 8; ++i) {
            m_oam[static_cast<std::size_t>(i)] =
                m_oam[static_cast<std::size_t>((src + i) & 0xFF)];
        }
    }

    if (m_evalDone) {
        return; // n wrapped past sprite 63: spin until the line ends (2a)
    }

    if (odd) {
        m_evalRead = m_oam[m_oamAddr];
        return;
    }

    const auto inRange = [&](std::uint8_t y) {
        return line >= y && line < y + height;
    };

    if (!m_evalOverflow) {
        // Steps 1-2: find and copy up to 8 in-range sprites.
        switch (m_evalStep) {
        case 0:
            if (inRange(m_evalRead)) {
                if (m_secCount < 8 && m_secByte < 32) {
                    m_oam2[static_cast<std::size_t>(m_secByte)] = m_evalRead;
                    if (m_secByte == 0 && (m_oamAddr >> 2) == 0) {
                        m_spriteZeroNext = true; // sprite 0 in range
                    }
                    ++m_secByte;
                }
                ++m_oamAddr; // advance to OAM[n][1] for the next odd read
                m_evalStep = 1; // copy OAM[n][1..3]
            } else {
                // Out of range: the slot does NOT advance (the next
                // candidate's Y overwrites the same position).
                const std::uint8_t next =
                    static_cast<std::uint8_t>(m_oamAddr + 4);
                if (next < m_oamAddr) {
                    m_evalDone = true; // n wrapped: all 64 evaluated
                }
                m_oamAddr = next; // n++
            }
            break;
        case 1:
        case 2:
            if (m_secCount < 8 && m_secByte < 32) {
                m_oam2[static_cast<std::size_t>(m_secByte)] = m_evalRead;
                ++m_secByte;
            }
            ++m_oamAddr;
            ++m_evalStep;
            break;
        default: // 3: last byte — sprite complete, n++ (address wraps to 4n)
            if (m_secCount < 8 && m_secByte < 32) {
                m_oam2[static_cast<std::size_t>(m_secByte)] = m_evalRead;
                ++m_secByte;
                ++m_secCount;
                const std::uint8_t next =
                    static_cast<std::uint8_t>(m_oamAddr + 1);
                if (next < m_oamAddr) {
                    m_evalDone = true; // n wrapped: all 64 evaluated
                }
                m_oamAddr = next; // 4n+3 -> 4(n+1): n++ completes the sprite
                if (m_secCount == 8) {
                    m_evalOverflow = true; // enter the step-3 scan
                    m_ovM = 0;
                    m_evalStep = 0;
                    break;
                }
            }
            m_evalStep = 0;
            break;
        }
        return;
    }

    // Steps 3-4: the buggy overflow scan. In the buggy state even-cycle
    // writes become secondary reads returning the first sprite's Y — a
    // value always in range — which can steer the increments (the bug).
    switch (m_evalStep) {
    case 0: { // comparing OAM[n][m] as a Y coordinate
        const std::uint8_t y = m_evalRead;
        if (inRange(y)) {
            m_status |= 0x20; // overflow flag
            m_evalStep = 2;   // read the next 3 entries
            m_ovM = 3;
        } else {
            // 3b: increment n and m (+5 in byte terms) — but at m==3,
            // m wraps and only n increments: 4n+3 -> 4(n+1)+0 is +1
            // (wiki 3a: "incrementing n when m overflows"). A flat +5
            // skips a sprite: the diagonal goes 9.0, 10.1, 11.2, 12.3,
            // 13.0, 14.1 (sprite_overflow 4.Obscure #2-#6).
            const std::uint8_t next = static_cast<std::uint8_t>(
                m_oamAddr + ((m_oamAddr & 3) == 3 ? 1 : 5));
            if (next < m_oamAddr) {
                m_evalStep = 3; // n wrapped: spin +4 until hblank
            }
            m_oamAddr = next;
        }
        break;
    }
    case 2: // consuming the 3 entries after a hit
        if (static_cast<std::uint8_t>(m_oamAddr + 1) < m_oamAddr) {
            m_evalDone = true; // n wrapped
        }
        ++m_oamAddr;
        if (--m_ovM == 0) {
            m_evalStep = 3; // step 4: spin
        }
        break;
    default: // 3: step 4 — failed copies, n++ each even dot
        m_oamAddr = static_cast<std::uint8_t>(m_oamAddr + 4);
        break;
    }
    m_secSecondaryRead = true; // writes disabled once 8 found
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
    // PPUCTRL bit 3 selects the 8x8 SPRITE pattern table.
    const bool spritePatternLeft = (m_ctrl & 0x08) != 0;
    m_unitCount = 0;
    m_spriteZeroActive = m_spriteZeroNext;
    for (int i = 0; i < 8; ++i) {
        const std::uint8_t y = m_oam2[static_cast<std::size_t>(i) * 4 + 0];
        const std::uint8_t tile = m_oam2[static_cast<std::size_t>(i) * 4 + 1];
        const std::uint8_t attr = m_oam2[static_cast<std::size_t>(i) * 4 + 2];
        int row = line - y - 1;
        if (row < 0 || row >= (tall ? 16 : 8)) {
            // Empty slot: the PPU still FETCHES tile $FF's pattern and
            // discards it — those dummy fetches drive A12 for the MMC3's
            // scanline counter (Mesen: "Fetches to sprite 0xFF for
            // remaining sprites/hidden - used by MMC3 IRQ counter").
            const std::uint16_t dummy = tall
                ? static_cast<std::uint16_t>(0x1000 + 0xFE * 16 + (row & 7))
                : static_cast<std::uint16_t>(
                    (spritePatternLeft ? 0x1000 : 0) + 0xFF * 16);
            notifyA12(dummy);
            notifyA12(static_cast<std::uint16_t>(dummy + 8));
            continue;
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
        notifyA12(addr);
        unit.lo = m_cart != nullptr ? m_cart->chrFetch(addr, true)
                                    : ppuBusRead(addr);
        notifyA12(static_cast<std::uint16_t>(addr + 8));
        unit.hi = m_cart != nullptr
            ? m_cart->chrFetch(static_cast<std::uint16_t>(addr + 8), true)
            : ppuBusRead(static_cast<std::uint16_t>(addr + 8));
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

    // Rendering off with v inside palette RAM: the PPU drives the entry v
    // points at instead of the backdrop (nesdev "PPU palettes").
    if (!renderingEnabled() && (m_v & 0x3F00) == 0x3F00) {
        index = static_cast<std::uint8_t>(m_v & 0x1F);
    }
    std::uint8_t color = paletteRead(index);
    if ((m_mask & 0x01) != 0) { // grayscale
        color &= 0x30;
    }
    std::uint32_t rgb = kPalette[color & 0x3F];
    if ((m_mask & 0xE0) != 0) { // colour emphasis bits 5-7
        rgb = m_emphasis[(m_mask >> 5) & 7][color & 0x3F];
    }
    if (m_frame != nullptr) {
        m_frame[static_cast<std::size_t>(m_scanline) * 256 + x] = rgb;
    }
}

// --- Dot timeline ---------------------------------------------------------------

void Ppu::stepDot()
{
    m_statusLag = m_status; // the read path sees the previous dot's status
    if (m_cart != nullptr && m_dot == 0) {
        m_cart->onScanline(m_scanline, renderingEnabled());
    }
    // The pre-render line is 261 (NTSC) or 311 (PAL); NTSC alone skips a dot
    // on odd frames while rendering.
    if (m_scanline == preRenderLine() && m_dot == 339 && m_oddFrame
        && oddFrameSkips() && renderingEnabled()) {
        m_dot = 340; // skip the final idle dot of the odd frame
    }

    // (A12 tracking is fetch-level: see notifyA12 / the fetch sites.)

    if (m_scanline < kVisibleLines) {
        if (m_dot >= 1 && m_dot <= 256) {
            renderPixel(static_cast<std::uint32_t>(m_dot - 1));
        }
        fetchCycle(static_cast<std::uint32_t>(m_dot));
        if (renderingEnabled()) {
            if (m_dot >= 1 && m_dot <= 256) {
                evalDot(static_cast<std::uint64_t>(m_dot));
            }
            if (m_dot == 0) {
                // New scanline: evaluation restarts (secondary was cleared
                // across dots 1-64; the machine state resets here).
                m_evalStep = 0;
                m_secCount = 0;
                m_secByte = 0;
                m_ovM = 0;
                m_evalOverflow = false;
            }
        }
        if (m_dot == 256) {
            // The dot-256/257/280-304 scroll adjustments only occur while
            // rendering; with rendering off v changes solely via CPU
            // accesses (nesdev PPU scrolling — "Between dot 328... and
            // 256 of the next scanline... if rendering is disabled, none
            // of these scroll adjustments occur"). Exercised by blargg
            // dmc_dma_during_read4's rendering-off delays.
            if (renderingEnabled()) {
                incrementY();
            }
        } else if (m_dot == 257) {
            if (renderingEnabled()) {
                copyHorizontal();
                m_oamAddr = 0; // OAMADDR resets during sprite fetchs
            }
        } else if (m_dot == kSpriteFetchDot) {
            // The eight sprite pattern fetches run dots ~257-320 (2 bytes
            // per 8-dot group after the garbage nametable fetches); the
            // units are consumed from dot 321 onward, so loading them at
            // the first pattern fetch keeps both the A12 timing and the
            // pipeline correct.
            if (renderingEnabled()) {
                loadSpriteUnits();
            }
        }
    } else if (m_scanline == preRenderLine()) {
        fetchCycle(static_cast<std::uint32_t>(m_dot));
        if (renderingEnabled()) {
            if (m_dot == 256) {
                incrementY();
            } else if (m_dot == 257) {
                copyHorizontal();
            } else if (m_dot >= 280 && m_dot <= 304) {
                copyVertical();
            } else if (m_dot == kSpriteFetchDot) {
                // No evaluation on the pre-render line (secondary OAM is
                // $FF), but the PPU still performs the eight DUMMY sprite
                // pattern fetches — and those CHR accesses drive A12 for
                // the MMC3's counter: blargg 4-scanline_timing's scanline-0
                // boundaries land on the PRE-RENDER line's fetch.
                loadSpriteUnits();
            }
        }
    }

    if (m_scanline == kVisibleLines - 1 && m_dot == 256) {
        m_frameComplete = true; // final visible pixel emitted (239,255)
    }
    if (m_scanline == kVblankLine && m_dot == timing::kVblSetDot) {
        m_status |= 0x80; // NMI line rises; the CPU samples at phi2
        if (std::getenv("RDDBG")) std::fprintf(stderr, "VBLSET master=%llu\n", (unsigned long long)m_sched.now());
    }
    if (m_scanline == preRenderLine() && m_dot == timing::kVblClearDot) {
        m_status &= static_cast<std::uint8_t>(~0xE0); // clear V/S0/O
        m_suppressSet = false;
        m_nmiKilled = false;
        m_oddFrame = !m_oddFrame;
    }

    ++m_dot;
    if (m_dot > 340) {
        m_dot = 0;
        ++m_scanline;
        if (m_scanline > preRenderLine()) {
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
    // PPU lags the CPU by one cycle at power-on (the hardware reset
    // synchronizer): the PPU state reflects dots completed by
    // master - 12 (T037 session 25; with the 8-cycle reset this yields
    // first-fetch PPU (0,21) per nestest.log).
    if (masterClock >= 12) {
        masterClock -= 12;
    } else {
        masterClock = 0;
    }
    const std::uint64_t targetDots = masterClock / m_profile.ppuDivisor;
    while (m_dots < targetDots) {
        stepDot();
        ++m_dots;
    }
}


void Ppu::debugReadRegisters(std::uint8_t dst[8]) const
{
    dst[0] = m_ctrl;
    dst[1] = m_mask;
    dst[2] = m_status; // no clear-on-read
    dst[3] = m_oamAddr;
    dst[4] = static_cast<std::uint8_t>(m_fineX & 7);
    dst[5] = static_cast<std::uint8_t>((m_v >> 8) & 0xFF);
    dst[6] = static_cast<std::uint8_t>(m_v & 0xFF);
    dst[7] = m_readBuffer;
}

void Ppu::debugReadVram(std::uint16_t addr, std::size_t len,
    std::uint8_t* dst) const
{
    for (std::size_t i = 0; i < len; ++i) {
        dst[i] = ppuBusRead(static_cast<std::uint16_t>(addr + i));
    }
}

void Ppu::debugWriteVram(std::uint16_t addr, const std::uint8_t* src,
    std::size_t len)
{
    for (std::size_t i = 0; i < len; ++i) {
        ppuBusWrite(static_cast<std::uint16_t>(addr + i), src[i]);
    }
}

void Ppu::debugReadOam(std::uint8_t dst[256]) const
{
    for (std::size_t i = 0; i < 256; ++i) {
        dst[i] = m_oam[i];
    }
}

void Ppu::debugWriteOam(const std::uint8_t src[256])
{
    for (std::size_t i = 0; i < 256; ++i) {
        m_oam[i] = src[i];
    }
}

void Ppu::debugReadPalette(std::uint8_t dst[32]) const
{
    for (int i = 0; i < 32; ++i) {
        dst[static_cast<std::size_t>(i)] =
            paletteRead(static_cast<std::uint8_t>(i));
    }
}

void Ppu::debugWritePalette(const std::uint8_t src[32])
{
    for (int i = 0; i < 32; ++i) {
        const auto idx = static_cast<std::size_t>(i);
        if ((i & 3) == 0) {
            m_palette[idx & 0x0F] = src[idx]; // alias -> backdrop copy
        } else {
            m_palette[idx] = src[idx];
        }
    }
}

void Ppu::saveState(StateWriter& w) const
{
    w.begin("PPU ");
    w.w(m_ctrl);
    w.w(m_mask);
    w.w(m_status);
    w.w(m_statusLag);
    w.w(m_v);
    w.w(m_t);
    w.w(m_fineX);
    w.w(m_w);
    w.w(m_readBuffer);
    w.w(m_decay);
    for (int bit = 0; bit < 8; ++bit) {
        w.w<std::uint64_t>(m_decayMaster[bit]);
    }
    w.w(m_oamAddr);
    w.wBytes(m_vram.data(), m_vram.size());
    w.wBytes(m_palette.data(), m_palette.size());
    w.wBytes(m_oam.data(), m_oam.size());
    w.wBytes(m_oam2.data(), m_oam2.size());
    w.w(m_shiftLo);
    w.w(m_shiftHi);
    w.w(m_shiftAttrLo);
    w.w(m_shiftAttrHi);
    w.w(m_latchNT);
    w.w(m_latchAT);
    w.w(m_latchLo);
    w.w(m_latchHi);
    for (const Unit& u : m_units) {
        w.w(u.x);
        w.w(u.attr);
        w.w(u.lo);
        w.w(u.hi);
    }
    w.w<std::uint64_t>(m_dots);
    w.w<std::int32_t>(m_scanline);
    w.w<std::int32_t>(m_dot);
    w.w(m_oddFrame);
    w.w(m_frameComplete);
    w.w(m_suppressSet);
    w.w(m_nmiKilled);
    w.w(m_ignoreWritesUntil);
    w.w(m_unitCount);
    w.w(m_spriteZeroNext);
    w.w(m_spriteZeroActive);
    w.w(m_evalStep);
    w.w(m_secCount);
    w.w(m_secByte);
    w.w(m_ovM);
    w.w(m_evalOverflow);
    w.w(m_secSecondaryRead);
    w.w(m_evalRead);
    w.end();
}

void Ppu::loadState(StateReader& r)
{
    m_ctrl = r.get<std::uint8_t>();
    m_mask = r.get<std::uint8_t>();
    m_status = r.get<std::uint8_t>();
    m_statusLag = r.get<std::uint8_t>();
    m_v = r.get<std::uint16_t>();
    m_t = r.get<std::uint16_t>();
    m_fineX = r.get<std::uint8_t>();
    m_w = r.get<std::uint8_t>();
    m_readBuffer = r.get<std::uint8_t>();
    m_decay = r.get<std::uint8_t>();
    for (int bit = 0; bit < 8; ++bit) {
        m_decayMaster[bit] = r.get<std::uint64_t>();
    }
    m_oamAddr = r.get<std::uint8_t>();
    r.getBytes(m_vram.data(), m_vram.size());
    r.getBytes(m_palette.data(), m_palette.size());
    r.getBytes(m_oam.data(), m_oam.size());
    r.getBytes(m_oam2.data(), m_oam2.size());
    m_shiftLo = r.get<std::uint16_t>();
    m_shiftHi = r.get<std::uint16_t>();
    m_shiftAttrLo = r.get<std::uint16_t>();
    m_shiftAttrHi = r.get<std::uint16_t>();
    m_latchNT = r.get<std::uint8_t>();
    m_latchAT = r.get<std::uint8_t>();
    m_latchLo = r.get<std::uint8_t>();
    m_latchHi = r.get<std::uint8_t>();
    for (Unit& u : m_units) {
        u.x = r.get<std::uint8_t>();
        u.attr = r.get<std::uint8_t>();
        u.lo = r.get<std::uint8_t>();
        u.hi = r.get<std::uint8_t>();
    }
    m_dots = r.get<std::uint64_t>();
    m_scanline = r.get<std::int32_t>();
    m_dot = r.get<std::int32_t>();
    m_oddFrame = r.get<std::uint8_t>() != 0;
    m_frameComplete = r.get<std::uint8_t>() != 0;
    m_suppressSet = r.get<std::uint8_t>() != 0;
    m_nmiKilled = r.get<std::uint8_t>() != 0;
    m_ignoreWritesUntil = r.get<std::uint64_t>();
    m_unitCount = r.get<std::int32_t>();
    m_spriteZeroNext = r.get<std::uint8_t>() != 0;
    m_spriteZeroActive = r.get<std::uint8_t>() != 0;
    m_evalStep = r.get<std::int32_t>();
    m_secCount = r.get<std::int32_t>();
    m_secByte = r.get<std::int32_t>();
    m_ovM = r.get<std::int32_t>();
    m_evalOverflow = r.get<std::uint8_t>() != 0;
    m_secSecondaryRead = r.get<std::uint8_t>() != 0;
    m_evalRead = r.get<std::uint8_t>();
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
