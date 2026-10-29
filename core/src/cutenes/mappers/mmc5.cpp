// MMC5 (iNES mapper 5) — Nintendo's ExROM ASIC. Session 43/44.
//
// Implemented surface (everything the corpus's four mapper-5 ROMs touch,
// plus the documented behavior needed by the games):
//   * PRG modes 0-3 ($5100), eight/16/32K banks ($5114-$5117), the
//     $5113-banked 64K PRG-RAM window and the $5102/$5103 write protect.
//   * CHR modes 0-3 ($5101), 10-bit banks $5120-$512B built per mode,
//     $5130 upper bits, and the 8x16 independent sprite banks.
//   * ExRAM ($5C00-$5FFF): CPU access rules per $5104 mode, nametable
//     mapping ($5105 incl. CIRAM page 0/1, ExRAM and fill mode), extended
//     attributes (mode 1) and vertical split (mode 0/1 + $5200-$5202).
//   * Scanline IRQ ($5203/$5204) at the start of the compared line and
//     the "in frame" status bit; 8x8 unsigned multiplier ($5205/$5206).
//   * MMC5 audio registers ($5000-$5015) are latched: the core has no
//     cartridge-audio mixing path (out of core scope, like VRC6).
//
// Not modeled: MMC5A-only registers ($5207-$520A, $5800-$5BFF) and the
// board's analog PCM output. Neither is exercised by any known NES game.

#include "cutenes/mappers/mapper.hpp"

#include "cutenes/cartridge/cart.hpp"
#include "cutenes/serialize/save_state.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace cutenes {
namespace {

class Mmc5 final : public Mapper {
public:
    explicit Mmc5(Cartridge& cart)
        : Mapper(cart)
    {
        // 64K PRG-RAM superset (nesdev: "emulating the PRG-RAM as 64K at
        // all times can be used as a compatible superset for all games").
        if (m_cart.prgRam.size() < 0x10000) {
            m_cart.prgRam.resize(0x10000, 0);
        }
        m_prgRegs[4] = 0xFF; // $5117 powers up to the last 8K bank
        m_chrRegs.fill(0);
    }

    bool ownsLowSpace() const override { return true; }

    // --- CPU bus -------------------------------------------------------------

    std::uint8_t cpuRead(std::uint16_t addr) override
    {
        if (addr >= 0x8000) {
            return readPrg(addr);
        }
        if (addr >= 0x6000) {
            return prgRamRead(addr); // $5113-banked window
        }
        if (addr >= 0x5C00) {
            return exramCpuRead(addr & 0x3FF);
        }
        if (addr >= 0x5000) {
            return readRegister(addr);
        }
        return 0;
    }

    void cpuWrite(std::uint16_t addr, std::uint8_t value) override
    {
        if (addr >= 0x8000) {
            prgMappedWrite(addr, value);
            return;
        }
        if (addr >= 0x6000) {
            prgRamWrite(addr, value);
            return;
        }
        if (addr >= 0x5C00) {
            exramCpuWrite(addr & 0x3FF, value);
            return;
        }
        if (addr < 0x5000) {
            return; // $4020-$4FFF: no MMC5 registers
        }
        if (addr <= 0x5015) {
            audioWrite(addr, value);
            return;
        }
        if (addr >= 0x5100 && addr <= 0x5130) {
            writeControl(addr, value);
            return;
        }
        if (addr >= 0x5200 && addr <= 0x5206) {
            writeIrq(addr, value);
        }
    }

    // --- PPU integration -----------------------------------------------------

    NtSource ntSource(std::uint16_t addr) const override
    {
        const std::uint16_t a = static_cast<std::uint16_t>(addr & 0x0FFF);
        switch ((m_ntMap >> ((a >> 10) * 2)) & 3) {
        case 0:
            return NtSource::Ciram0;
        case 1:
            return NtSource::Ciram1;
        default:
            return NtSource::Mapper;
        }
    }

    std::uint8_t ntRead(std::uint16_t addr) const override
    {
        const std::uint16_t a = static_cast<std::uint16_t>(addr & 0x0FFF);
        const int slot = a >> 10;
        const int map = (m_ntMap >> (slot * 2)) & 3;
        if (map == 2) { // ExRAM
            if (m_exramMode >= 2) {
                return 0; // modes 2/3: nametables read as zeros
            }
            return m_exram[a & 0x3FF];
        }
        if (map == 3) { // fill mode
            if ((a & 0x3C0) == 0x3C0) {
                const std::uint8_t fill = static_cast<std::uint8_t>(m_fillAttr & 3);
                return static_cast<std::uint8_t>(
                    (fill << 6) | (fill << 4) | (fill << 2) | fill);
            }
            return m_fillTile;
        }
        return 0; // CIRAM slots are handled by the PPU
    }

    void ntWrite(std::uint16_t addr, std::uint8_t value) override
    {
        const std::uint16_t a = static_cast<std::uint16_t>(addr & 0x0FFF);
        const int map = (m_ntMap >> ((a >> 10) * 2)) & 3;
        if (map == 2 && m_exramMode < 2) {
            m_exram[a & 0x3FF] = value;
        }
        // Fill-mode nametables are generated from $5106/$5107: writes drop.
    }

    void setPpuCtrl(std::uint8_t value) override { m_ppuCtrl = value; }
    void setPpuMask(std::uint8_t value) override { m_ppuMask = value; }

    std::uint8_t chrReadBus(std::uint16_t addr, bool sprite) const override
    {
        const bool tall = (m_ppuCtrl & 0x20) != 0;
        const bool substitutions = (m_ppuMask & 0x18) != 0;

        if (sprite && tall && substitutions) {
            // 8x16 sprites: eight independent 1K banks ($5120-$5127).
            const std::uint32_t bank =
                m_chrRegs[static_cast<std::size_t>((addr >> 10) & 7)] & 0x3FF;
            return chrReadRaw(bank * 0x400 + (addr & 0x3FF));
        }
        return chrReadRaw(chrAddress(addr, sprite));
    }

    std::uint8_t chrRead(std::uint16_t addr) const override
    {
        // $2007 sees whichever bank set was written last ($5120-$5127 for
        // sprites, $5128-$512B for background).
        return chrReadRaw(chrAddress(addr, m_lastChrSet == 0));
    }

    void chrWrite(std::uint16_t addr, std::uint8_t value) override
    {
        if (m_cart.chr.empty() && !m_cart.chrRam.empty()) {
            m_cart.chrRam[addr % m_cart.chrRam.size()] = value;
        }
    }

    bool bgFetch(BgFetch& fetch) const override
    {
        const bool substitutions = (fetch.mask & 0x18) != 0;
        if (!substitutions || fetch.scanline < 0 || fetch.scanline > 239) {
            return false;
        }
        const bool split = splitActive(fetch.tileColumn);
        if (split) {
            const int row = splitRow(fetch.scanline);
            const int col = splitColumn(fetch.tileColumn);
            if (fetch.pattern) {
                const std::uint32_t fine =
                    static_cast<std::uint32_t>(splitFine(fetch.scanline));
                fetch.addr = (static_cast<std::uint32_t>(m_splitBank) << 12)
                    | (fetch.addr & 0x7F8u) | fine;
                return true;
            }
            if (fetch.attribute) {
                fetch.value = m_exram[static_cast<std::size_t>(
                    960 + (row >> 2) * 8 + (col >> 2))];
            } else {
                fetch.value = m_exram[static_cast<std::size_t>(row * 32 + col)];
            }
            return true;
        }
        if (m_exramMode == 1 && (m_ppuMask & 0x18) != 0) {
            const std::uint8_t ext =
                m_exram[static_cast<std::size_t>(fetch.v & 0x3FF)];
            if (fetch.attribute) {
                fetch.value = ext;
                return true;
            }
            if (fetch.pattern) {
                const std::uint32_t bank4 = static_cast<std::uint32_t>(
                    (ext & 0x3F) | ((m_chrUpper & 3) << 6));
                fetch.addr = bank4 * 0x1000 + (fetch.addr & 0xFFFu);
                return true;
            }
        }
        return false;
    }

    void onScanline(int line, bool rendering) override
    {
        if (!rendering || line < 0 || line > 239) {
            m_inFrame = false;
            return;
        }
        if (line == 0) {
            m_inFrame = true;
            m_scanlineCounter = 0;
            return;
        }
        if (!m_inFrame) {
            m_inFrame = true;
            m_scanlineCounter = 0;
        }
        ++m_scanlineCounter;
        if (m_irqTarget != 0 && m_scanlineCounter == m_irqTarget) {
            m_irqPending = true;
        }
    }

    bool irqLine() const override { return m_irqPending && m_irqEnabled; }

    Mirroring mirroring() const override { return Mirroring::MapperControlled; }

    // --- Save state ----------------------------------------------------------

    void saveState(StateWriter& w) const override
    {
        w.begin("MAP ");
        w.w<std::uint16_t>(m_cart.info.mapperId);
        w.w(m_prgMode);
        w.w(m_chrMode);
        w.w(m_exramMode);
        w.w(m_ntMap);
        w.w(m_fillTile);
        w.w(m_fillAttr);
        w.w(m_ppuCtrl);
        w.w(m_ppuMask);
        w.w(m_protect1);
        w.w(m_protect2);
        w.w(m_prgRamBank);
        for (const std::uint8_t v : m_prgRegs) {
            w.w(v);
        }
        for (const std::uint16_t v : m_chrRegs) {
            w.w(v);
        }
        w.w(m_chrUpper);
        w.w(m_lastChrSet);
        w.w(m_splitReg);
        w.w(m_splitScroll);
        w.w(m_splitBank);
        w.w(m_irqTarget);
        w.w<std::uint8_t>(m_irqEnabled ? 1 : 0);
        w.w<std::uint8_t>(m_irqPending ? 1 : 0);
        w.w<std::uint8_t>(m_inFrame ? 1 : 0);
        w.w(m_scanlineCounter);
        w.w(m_multiplierA);
        w.w(m_multiplierB);
        w.w<std::uint16_t>(m_product);
        w.w(m_pcmIrq);
        w.wBytes(m_exram.data(), m_exram.size());
        w.end();
    }

    void loadState(StateReader& r) override
    {
        (void)r.get<std::uint16_t>(); // mapperId
        m_prgMode = r.get<std::uint8_t>();
        m_chrMode = r.get<std::uint8_t>();
        m_exramMode = r.get<std::uint8_t>();
        m_ntMap = r.get<std::uint8_t>();
        m_fillTile = r.get<std::uint8_t>();
        m_fillAttr = r.get<std::uint8_t>();
        m_ppuCtrl = r.get<std::uint8_t>();
        m_ppuMask = r.get<std::uint8_t>();
        m_protect1 = r.get<std::uint8_t>();
        m_protect2 = r.get<std::uint8_t>();
        m_prgRamBank = r.get<std::uint8_t>();
        for (auto& v : m_prgRegs) {
            v = r.get<std::uint8_t>();
        }
        for (auto& v : m_chrRegs) {
            v = r.get<std::uint16_t>();
        }
        m_chrUpper = r.get<std::uint8_t>();
        m_lastChrSet = r.get<std::uint8_t>();
        m_splitReg = r.get<std::uint8_t>();
        m_splitScroll = r.get<std::uint8_t>();
        m_splitBank = r.get<std::uint8_t>();
        m_irqTarget = r.get<std::uint8_t>();
        m_irqEnabled = r.get<std::uint8_t>() != 0;
        m_irqPending = r.get<std::uint8_t>() != 0;
        m_inFrame = r.get<std::uint8_t>() != 0;
        m_scanlineCounter = r.get<std::uint8_t>();
        m_multiplierA = r.get<std::uint8_t>();
        m_multiplierB = r.get<std::uint8_t>();
        m_product = r.get<std::uint16_t>();
        m_pcmIrq = r.get<std::uint8_t>();
        r.getBytes(m_exram.data(), m_exram.size());
    }

private:
    // --- ExRAM ---------------------------------------------------------------

    std::uint8_t exramCpuRead(std::size_t index) const
    {
        // Modes 0/1 hide ExRAM from the CPU (open bus); 2/3 allow reads.
        return m_exramMode >= 2 ? m_exram[index] : 0;
    }

    void exramCpuWrite(std::size_t index, std::uint8_t value)
    {
        const bool rendering = (m_ppuMask & 0x18) != 0;
        switch (m_exramMode) {
        case 0:
        case 1:
            // Counterintuitive hardware rule: writes only while rendering.
            if (rendering) {
                m_exram[index] = value;
            }
            break;
        case 2:
            m_exram[index] = value;
            break;
        default: // 3: read-only
            break;
        }
    }

    // --- PRG -----------------------------------------------------------------

    std::uint8_t prgRamRead(std::uint16_t addr) const
    {
        if (m_cart.prgRam.empty()) {
            return 0;
        }
        const std::size_t bank = static_cast<std::size_t>(m_prgRamBank & 7);
        const std::size_t index =
            (bank * 0x2000 + (addr & 0x1FFF)) % m_cart.prgRam.size();
        return m_cart.prgRam[index];
    }

    void prgRamWrite(std::uint16_t addr, std::uint8_t value)
    {
        // Both protect registers must hold their unlock pattern.
        if ((m_protect1 & 3) != 2 || (m_protect2 & 3) != 1
            || m_cart.prgRam.empty()) {
            return;
        }
        const std::size_t bank = static_cast<std::size_t>(m_prgRamBank & 7);
        const std::size_t index =
            (bank * 0x2000 + (addr & 0x1FFF)) % m_cart.prgRam.size();
        m_cart.prgRam[index] = value;
    }

    void prgMappedWrite(std::uint16_t addr, std::uint8_t value)
    {
        // Writes into $8000-$FFFF land in PRG-RAM only when the window's
        // bank register selects RAM ($5114-$5116 bit 7 = 0); $5117 windows
        // are ROM-only. ROM or register regions: writes drop.
        std::uint8_t reg = 0;
        std::size_t size = 0x2000;
        switch (m_prgMode) {
        case 0:
            return; // $5117 window, ROM only
        case 1:
            if (addr >= 0xC000) {
                return;
            }
            reg = m_prgRegs[2];
            size = 0x4000;
            break;
        case 2:
            if (addr >= 0xE000) {
                return;
            }
            if (addr >= 0xC000) {
                reg = m_prgRegs[3];
                size = 0x2000;
            } else {
                reg = m_prgRegs[2];
                size = 0x4000;
            }
            break;
        default:
            if (addr >= 0xE000) {
                return;
            }
            if (addr < 0xA000) {
                reg = m_prgRegs[1];
            } else if (addr < 0xC000) {
                reg = m_prgRegs[2];
            } else {
                reg = m_prgRegs[3];
            }
            size = 0x2000;
            break;
        }
        if ((reg & 0x80) != 0 || m_cart.prgRam.empty()) {
            return;
        }
        if ((m_protect1 & 3) != 2 || (m_protect2 & 3) != 1) {
            return; // same write protect gate as the $6000 window
        }
        const std::size_t off = prgBankOffset(reg, size, addr);
        m_cart.prgRam[off % m_cart.prgRam.size()] = value;
    }

    std::size_t prgBankOffset(std::uint8_t reg, std::size_t size,
        std::uint16_t addr) const
    {
        // 8K banks use reg bits 6..0, 16K bits 6..1 (CPU A13 supplies the
        // low bit), 32K bits 6..2 (CPU A14..A13 pass through).
        std::size_t bank = 0;
        switch (size) {
        case 0x2000:
            bank = static_cast<std::size_t>(reg & 0x7F);
            return bank * size + (addr & (size - 1));
        case 0x4000:
            bank = static_cast<std::size_t>((reg & 0x7F) >> 1);
            return bank * size + (addr & (size - 1));
        default:
            bank = static_cast<std::size_t>((reg & 0x7F) >> 2);
            return bank * size + (addr & (size - 1));
        }
    }

    std::uint8_t prgRomRead(std::uint8_t reg, std::size_t size,
        std::uint16_t addr) const
    {
        const std::size_t off = prgBankOffset(reg, size, addr);
        return m_cart.prg.empty() ? 0 : m_cart.prg[off % m_cart.prg.size()];
    }

    std::uint8_t prgRamMappedRead(std::uint8_t reg, std::size_t size,
        std::uint16_t addr) const
    {
        const std::size_t off = prgBankOffset(reg, size, addr);
        return m_cart.prgRam.empty()
            ? 0
            : m_cart.prgRam[off % m_cart.prgRam.size()];
    }

    std::uint8_t readBank(std::uint8_t reg, std::size_t size,
        std::uint16_t addr) const
    {
        // $5114-$5116 bit 7 selects ROM (1) or RAM (0). $5117 windows are
        // ROM-only and never route here.
        if ((reg & 0x80) == 0) {
            return prgRamMappedRead(reg, size, addr);
        }
        return prgRomRead(reg, size, addr);
    }

    std::uint8_t readPrg(std::uint16_t addr) const
    {
        switch (m_prgMode) {
        case 0: // one 32K bank at $8000 from $5117
            return prgRomRead(m_prgRegs[4], 0x8000, addr);
        case 1:
            if (addr < 0xC000) {
                return readBank(m_prgRegs[2], 0x4000, addr);
            }
            return prgRomRead(m_prgRegs[4], 0x4000, addr);
        case 2:
            if (addr < 0xC000) {
                return readBank(m_prgRegs[2], 0x4000, addr);
            }
            if (addr < 0xE000) {
                return readBank(m_prgRegs[3], 0x2000, addr);
            }
            return prgRomRead(m_prgRegs[4], 0x2000, addr);
        default: // 3: four 8K windows
            if (addr < 0xA000) {
                return readBank(m_prgRegs[1], 0x2000, addr);
            }
            if (addr < 0xC000) {
                return readBank(m_prgRegs[2], 0x2000, addr);
            }
            if (addr < 0xE000) {
                return readBank(m_prgRegs[3], 0x2000, addr);
            }
            return prgRomRead(m_prgRegs[4], 0x2000, addr);
        }
    }

    // --- CHR -----------------------------------------------------------------

    std::uint32_t chrAddress(std::uint32_t addr, bool sprite) const
    {
        const std::uint32_t size = 0x400u << (3 - m_chrMode);
        const std::uint32_t unit = size / 0x400u;
        std::uint32_t a = addr & 0x1FFFu;
        int index = 0;
        if (sprite) {
            index = static_cast<int>((unit - 1) + (a / size) * unit);
        } else {
            if (size < 0x2000) {
                a &= 0x0FFFu; // the four background regs cover 4K, folded
            }
            if (size == 0x2000) {
                index = 3; // 8K mode: background uses $512B for all of CHR
            } else {
                index = static_cast<int>(
                    (unit - 1) + ((a & 0x0FFF) / size) * unit);
            }
            index += 8;
        }
        if (index < 0 || index > 11) {
            index = 11;
        }
        const std::uint32_t bank1k =
            m_chrRegs[static_cast<std::size_t>(index)] & 0x3FF;
        return bank1k * 0x400u + (a & (size - 1));
    }

    // CHR registers hold a bank number in units of the current bank size
    // (8K, 4K, 2K or 1K by $5101), with bits 8-9 from $5130. It is stored as
    // a 1K bank number so every read path uses the same units.
    void writeChrReg(std::size_t index, std::uint8_t value)
    {
        const std::uint32_t unit = 1u << (3 - m_chrMode); // 1K banks per unit
        const std::uint32_t number =
            (static_cast<std::uint32_t>(m_chrUpper & 3) << 8) | value;
        const std::uint32_t v = number * unit;
        m_chrRegs[index] = static_cast<std::uint16_t>(v & 0x3FF);
        m_lastChrSet = index < 8 ? 0 : 8;
    }

    // --- Registers -----------------------------------------------------------

    std::uint8_t readRegister(std::uint16_t addr)
    {
        switch (addr) {
        case 0x5010:
            return m_pcmIrq ? 1 : 0; // PCM IRQ pending
        case 0x5204:
            {
                // Reading acknowledges the scanline IRQ (the pending flag
                // reports the pre-acknowledge state).
                const std::uint8_t value = static_cast<std::uint8_t>(
                    (m_irqPending ? 0x80 : 0)
                    | (m_inFrame ? 0x40 : 0));
                m_irqPending = false;
                return value;
            }
        case 0x5205:
            return static_cast<std::uint8_t>(m_product & 0xFF);
        case 0x5206:
            return static_cast<std::uint8_t>(m_product >> 8);
        default:
            return 0; // write-only register, open bus convention
        }
    }

    void writeControl(std::uint16_t addr, std::uint8_t value)
    {
        switch (addr) {
        case 0x5100:
            m_prgMode = static_cast<std::uint8_t>(value & 3);
            break;
        case 0x5101:
            m_chrMode = static_cast<std::uint8_t>(value & 3);
            break;
        case 0x5102:
            m_protect1 = value;
            break;
        case 0x5103:
            m_protect2 = value;
            break;
        case 0x5104:
            m_exramMode = static_cast<std::uint8_t>(value & 3);
            break;
        case 0x5105:
            m_ntMap = value;
            break;
        case 0x5106:
            m_fillTile = value;
            break;
        case 0x5107:
            m_fillAttr = static_cast<std::uint8_t>(value & 3);
            break;
        case 0x5113:
            m_prgRamBank = value;
            break;
        case 0x5114:
        case 0x5115:
        case 0x5116:
        case 0x5117:
            m_prgRegs[static_cast<std::size_t>(addr - 0x5113)] = value;
            break;
        case 0x5130:
            m_chrUpper = static_cast<std::uint8_t>(value & 3);
            break;
        default:
            if (addr >= 0x5120 && addr <= 0x512B) {
                writeChrReg(static_cast<std::size_t>(addr - 0x5120), value);
            }
            break;
        }
    }

    void writeIrq(std::uint16_t addr, std::uint8_t value)
    {
        switch (addr) {
        case 0x5200:
            m_splitReg = value;
            break;
        case 0x5201:
            m_splitScroll = value;
            break;
        case 0x5202:
            m_splitBank = value;
            break;
        case 0x5203:
            m_irqTarget = value;
            break;
        case 0x5204:
            m_irqEnabled = (value & 0x80) != 0;
            break;
        case 0x5205:
            m_multiplierA = value;
            m_product = static_cast<std::uint16_t>(
                static_cast<std::uint16_t>(m_multiplierA) * m_multiplierB);
            break;
        case 0x5206:
            m_multiplierB = value;
            m_product = static_cast<std::uint16_t>(
                static_cast<std::uint16_t>(m_multiplierA) * m_multiplierB);
            break;
        default:
            break;
        }
    }

    void audioWrite(std::uint16_t addr, std::uint8_t value)
    {
        if (addr == 0x5010) {
            // bit 7: PCM IRQ enable, bit 0: PCM IRQ pending (write)
            m_pcmIrq = static_cast<std::uint8_t>(value & 1);
        }
        // Registers $5000-$5007/$5011-$5015 are latched but not mixed:
        // the core exposes no cartridge-audio path (see the file comment).
        if (addr >= 0x5000 && addr <= 0x5015) {
            m_audio[static_cast<std::size_t>(addr - 0x5000)] = value;
        }
    }

    // --- Vertical split helpers ---------------------------------------------

    bool splitActive(int tileColumn) const
    {
        if ((m_splitReg & 0x80) == 0 || m_exramMode >= 2) {
            return false;
        }
        const int threshold = m_splitReg & 0x1F;
        if ((m_splitReg & 0x40) != 0) {
            return tileColumn >= threshold;
        }
        return tileColumn < threshold;
    }

    int splitRow(int scanline) const
    {
        const int total = (static_cast<int>(m_splitScroll) + scanline) % 240;
        return (total >> 3) % 30;
    }

    int splitFine(int scanline) const
    {
        const int total = (static_cast<int>(m_splitScroll) + scanline) % 240;
        return total & 7;
    }

    static int splitColumn(int tileColumn)
    {
        // Right-side split content is right-aligned with the nametable;
        // left-side content is left-aligned. Fetch columns 0-1 hold the
        // next line's first two tiles.
        return tileColumn >= 2 ? tileColumn - 2 : 0;
    }

    // --- State ---------------------------------------------------------------

    std::uint8_t m_prgMode = 3;  // $5100 powers up in mode 3
    std::uint8_t m_chrMode = 0;
    std::uint8_t m_exramMode = 3; // $5104 reset value
    std::uint8_t m_ntMap = 0;
    std::uint8_t m_fillTile = 0;
    std::uint8_t m_fillAttr = 0;
    std::uint8_t m_ppuCtrl = 0;
    std::uint8_t m_ppuMask = 0;
    std::uint8_t m_protect1 = 1; // locked reset values
    std::uint8_t m_protect2 = 2;
    std::uint8_t m_prgRamBank = 0;
    // $5113-$5117; $5117 reset = $FF (vectors must stay in ROM).
    std::array<std::uint8_t, 5> m_prgRegs{};
    std::array<std::uint16_t, 12> m_chrRegs{};
    std::uint8_t m_chrUpper = 0;
    std::uint8_t m_lastChrSet = 0;
    std::uint8_t m_splitReg = 0;
    std::uint8_t m_splitScroll = 0;
    std::uint8_t m_splitBank = 0;
    std::uint8_t m_irqTarget = 0;
    bool m_irqEnabled = false;
    bool m_irqPending = false;
    bool m_inFrame = false;
    std::uint8_t m_scanlineCounter = 0;
    std::uint8_t m_multiplierA = 0;
    std::uint8_t m_multiplierB = 0;
    std::uint16_t m_product = 0;
    std::uint8_t m_pcmIrq = 0;
    std::array<std::uint8_t, 0x16> m_audio{};
    std::array<std::uint8_t, 1024> m_exram{};
};

} // namespace

std::unique_ptr<Mapper> makeMmc5(Cartridge& cart)
{
    return std::make_unique<Mmc5>(cart);
}

} // namespace cutenes
