// MMC1 (mapper 1) — task T053 (pulled forward: every blargg console test
// ROM is an MMC1/SXROM board, so the tier-1 suite needs it).
//
// 5-bit shift-latch write protocol (the fifth write commits), PRG bank
// modes 0-3 (32K / 16K fixed-low / 16K fixed-high), CHR 4K/8K banks,
// mirroring control, and battery WRAM. Per nesdev MMC1 documentation.
//
// ASIC revisions (nesdev MMC1, "ASIC Revisions"; iNES mapper 155 = MMC1A,
// mapper 1 = MMC1B): on the MMC1A, bit 4 of the PRG-bank register ($E000)
// does NOT gate PRG-RAM (always enabled) — instead it makes register bit 3
// drive PRG A17 directly across the whole $8000-$FFFF range, bypassing the
// fixed-bank logic. On the MMC1B, bit 4 disables PRG-RAM (reads float,
// writes dropped). Both revisions ignore the data bit of a serial-port
// write that immediately follows another on the next cycle (the 6502 RMW
// double write); the bit-7 reset is never ignored.

#include "cutenes/mappers/mapper.hpp"

#include "cutenes/cartridge/cart.hpp"

#include "cutenes/serialize/save_state.hpp"
#include "cutenes/system/scheduler.hpp"

#include <cstring>

namespace cutenes {
namespace {

class Mmc1 final : public Mapper {
public:
    using Mapper::Mapper;

    void setRevisionA(bool revisionA) override
    {
        m_revisionA = revisionA;
    }

    void attachScheduler(Scheduler& scheduler) override
    {
        m_sched = &scheduler;
    }

    std::uint8_t cpuRead(std::uint16_t addr) override
    {
        if (addr < 0x8000) {
            return prgRam(addr);
        }
        const std::size_t bank = prgBankFor(addr);
        return bankByte(bank, addr & 0x3FFF);
    }

    void cpuWrite(std::uint16_t addr, std::uint8_t value) override
    {
        if (addr < 0x8000) {
            return; // PRG-RAM writes are gated by prgRamWritable()
        }
        // Consecutive-cycle writes: a serial-port write on the cycle right
        // after another contributes no data bit (the 6502 RMW double
        // write). The bit-7 reset always applies (nesdev MMC1,
        // "Consecutive-cycle writes").
        bool consecutive = false;
        if (m_sched != nullptr) {
            const std::uint64_t now = m_sched->now();
            consecutive = m_hadWrite && (now - m_lastWriteMaster) <= 12;
            m_lastWriteMaster = now;
            m_hadWrite = true;
        }
        if ((value & 0x80) != 0) {
            // Reset: shift register cleared, control bit 3 (PRG mode) set.
            m_shift = 0x10;
            m_control = static_cast<std::uint8_t>(m_control | 0x0C);
            return;
        }
        if (consecutive) {
            return;
        }
        // A sentinel bit in the shift register marks the fifth write: it sits
        // at bit 0 once four data bits have been shifted in.
        const bool commit = (m_shift & 0x01) != 0;
        m_shift = static_cast<std::uint8_t>(
            ((value & 1) << 4) | (m_shift >> 1));
        if (!commit) {
            return;
        }
        const std::uint8_t reg = (addr >> 13) & 3; // $8000/$A000/$C000/$E000
        const std::uint8_t data = m_shift & 0x1F;
        m_shift = 0x10;
        switch (reg) {
        case 0: // control: mirroring, PRG mode, CHR mode
            m_control = data;
            break;
        case 1: // CHR bank 0
            m_chrBank0 = data;
            break;
        case 2: // CHR bank 1
            m_chrBank1 = data;
            break;
        default: // PRG bank: bits 0-3; bit 4 per revision (header comment)
            m_prgBank = static_cast<std::uint8_t>(data & 0x0F);
            if (m_revisionA) {
                m_a17Bypass = (data & 0x10) != 0;
            } else {
                m_wramDisabled = (data & 0x10) != 0;
            }
            break;
        }
    }

    // MMC1A: PRG-RAM always enabled; MMC1B: $E000 bit 4 gates it.
    bool prgRamWritable() const override
    {
        return m_revisionA || !m_wramDisabled;
    }
    bool prgRamReadable() const override
    {
        return m_revisionA || !m_wramDisabled;
    }

    std::uint8_t chrRead(std::uint16_t addr) const override
    {
        if (m_cart.chr.empty()) { // CHR-RAM (8K default)
            return m_cart.chrRam.empty() ? 0
                : m_cart.chrRam[chrIndex(addr) % m_cart.chrRam.size()];
        }
        return m_cart.chr[chrIndex(addr) % m_cart.chr.size()];
    }

    void chrWrite(std::uint16_t addr, std::uint8_t value) override
    {
        if (m_cart.chr.empty() && !m_cart.chrRam.empty()) {
            m_cart.chrRam[chrIndex(addr) % m_cart.chrRam.size()] = value;
        }
    }

    Mirroring mirroring() const override
    {
        switch (m_control & 3) {
        case 0: return Mirroring::Vertical;    // one-screen lo uses NT $2000
        case 1: return Mirroring::Horizontal;
        case 2: return Mirroring::Vertical; // approximation: one-screen lo
        case 3: return Mirroring::Horizontal; // approximation: one-screen hi
        }
        return m_cart.info.mirroring;
    }

private:
    std::uint8_t prgRam(std::uint16_t addr) const
    {
        if (m_cart.prgRam.empty()) {
            return 0;
        }
        return m_cart.prgRam[addr & (m_cart.prgRam.size() - 1)];
    }

    // Byte offset into CHR for a pattern address under the current CHR mode.
    // Bank registers are in 4K units; in 8K mode bit 0 of the value is ignored.
    std::size_t chrIndex(std::uint16_t addr) const
    {
        if ((m_control & 0x10) == 0) {
            return static_cast<std::size_t>(m_chrBank0 & 0x1E) * 4096
                + (addr & 0x1FFF);
        }
        const std::uint8_t bank = addr < 0x1000 ? m_chrBank0 : m_chrBank1;
        return static_cast<std::size_t>(bank & 0x1F) * 4096 + (addr & 0x0FFF);
    }

    std::size_t prgBankFor(std::uint16_t addr) const
    {
        const std::size_t banks = m_cart.prg.size() / 0x4000;
        const std::uint8_t mode = (m_control >> 2) & 3;
        const std::size_t bank = m_prgBank & (banks > 0 ? banks - 1 : 0);
        switch (mode) {
        case 0:
        case 1: { // 32K switchable at $8000
            const std::size_t b32 = (m_prgBank & 0x0E) & (banks - 1);
            return (addr < 0xC000) ? b32 : (b32 + 1) & (banks - 1);
        }
        case 2: // 16K fixed low, switchable high
            return (addr < 0xC000) ? fixedBank(banks, 0) : bank;
        default: // 3: switchable low, fixed high
            return (addr < 0xC000) ? bank : fixedBank(banks, banks - 1);
        }
    }

    // The fixed window's bank. On the MMC1A with the $E000 bypass bit set,
    // register bit 3 drives PRG A17 directly (the fixed bank's own A17 is
    // overridden — nesdev MMC1, "ASIC Revisions"); only visible with
    // 256 KiB PRG (16 banks).
    std::size_t fixedBank(std::size_t banks, std::size_t bank) const
    {
        if (m_revisionA && m_a17Bypass) {
            const std::size_t a17 = (m_prgBank >> 3) & 1;
            bank &= 0x07;
            bank |= a17 << 3;
        }
        return bank & (banks > 0 ? banks - 1 : 0);
    }

    std::uint8_t bankByte(std::size_t bank, std::uint16_t offset) const
    {
        return m_cart.prg[(bank * 0x4000 + offset) % m_cart.prg.size()];
    }

    std::uint8_t m_control = 0x0C; // PRG mode 3, CHR 8K
    std::uint8_t m_chrBank0 = 0, m_chrBank1 = 0, m_prgBank = 0;
    std::uint8_t m_shift = 0x10;
    bool m_wramDisabled = false;   // MMC1B: $E000 bit 4
    bool m_revisionA = false;      // mapper 155
    bool m_a17Bypass = false;      // MMC1A: $E000 bit 4
    Scheduler* m_sched = nullptr;  // write-cycle stamps (consecutive rule)
    std::uint64_t m_lastWriteMaster = 0;
    bool m_hadWrite = false;

public:
    void saveState(StateWriter& w) const override
    {
        w.begin("MAP ");
        w.w<std::uint16_t>(m_cart.info.mapperId);
        w.w(m_control);
        w.w(m_chrBank0);
        w.w(m_chrBank1);
        w.w(m_prgBank);
        w.w(m_shift);
        w.w(m_wramDisabled);
        w.w<std::uint8_t>(m_revisionA ? 1 : 0);
        w.w<std::uint8_t>(m_a17Bypass ? 1 : 0);
        w.end();
    }

    void loadState(StateReader& r) override
    {
        (void)r.get<std::uint16_t>(); // mapperId
        m_control = r.get<std::uint8_t>();
        m_chrBank0 = r.get<std::uint8_t>();
        m_chrBank1 = r.get<std::uint8_t>();
        m_prgBank = r.get<std::uint8_t>();
        m_shift = r.get<std::uint8_t>();
        m_wramDisabled = r.get<std::uint8_t>() != 0;
        m_revisionA = r.get<std::uint8_t>() != 0;
        m_a17Bypass = r.get<std::uint8_t>() != 0;
    }
};

} // namespace

std::unique_ptr<Mapper> makeMmc1(Cartridge& cart)
{
    return std::make_unique<Mmc1>(cart);
}

} // namespace cutenes
