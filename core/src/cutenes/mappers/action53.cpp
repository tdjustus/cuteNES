// Action 53 mapper (iNES 28, GTROM class) — session 43. Registers are
// selected through $5000 and written through $8000-$FFFF (no bus
// conflicts): $5000 = select register (bit 7 = supervisor), $8000+ =
// write the selected register. The corpus's Streemerz bundle and
// test28 (third_party/test28, 0.04) use this board.
//
//   $00 (user)     CHR RAM A13-A14 = D0-D1; D4 is written to mirroring
//                  bit 0 when the mirroring mode is 0/1 (one-screen)
//   $01 (user)     PRG inner bank = D0-D3; D4 as above
//   $80 (supervisor) mode: D0-D1 mirroring (0/1 one-screen lo/hi,
//                  2 vertical, 3 horizontal), D2-D3 PRG mode (0/1 = 32K
//                  switchable, 2 = fixed-low/switch-high, 3 = UNROM),
//                  D4-D5 outer-bank size (32K..256K granularity)
//   $81 (supervisor) outer bank
//
// CHR is 8K RAM (GTROM) or banks of a 128K CHR-ROM on some releases; the
// write-through-$8000 path also selects the PRG register directly on
// power-up default state (register 1 preselected).

#include "cutenes/mappers/mapper.hpp"

#include "cutenes/cartridge/cart.hpp"

#include "cutenes/serialize/save_state.hpp"

namespace cutenes {
namespace {

class Action53 final : public Mapper {
public:
    using Mapper::Mapper;

    std::uint8_t cpuRead(std::uint16_t addr) override
    {
        if (addr >= 0x8000) {
            return prgByte(addr);
        }
        return 0;
    }

    // The register select at $5000-$5FFF is write-only: reads are open bus.
    bool decodesLowWrites() const override { return true; }

    void cpuWrite(std::uint16_t addr, std::uint8_t value) override
    {
        if (addr >= 0x5000 && addr < 0x6000) {
            m_sel = static_cast<std::uint8_t>(value & 0x81);
            return;
        }
        if (addr < 0x8000) {
            return;
        }
        switch (m_sel) {
        case 0x00:
            m_chrA13 = static_cast<std::uint8_t>(value & 0x03);
            writeMirroringBit(value);
            break;
        case 0x01:
            m_inner = static_cast<std::uint8_t>(value & 0x0F);
            writeMirroringBit(value);
            break;
        case 0x80:
            m_mode = value;
            break;
        case 0x81:
            m_outer = value;
            break;
        default:
            break;
        }
    }

    // Mode bits 0-1: 0/1 = one-screen lower/upper, 2 = vertical,
    // 3 = horizontal. Bit 0 is also written by D4 of $00/$01 (see
    // writeMirroringBit), so the page follows whichever wrote it last.
    Mirroring mirroring() const override
    {
        switch (m_mode & 3) {
        case 2: return Mirroring::Vertical;
        case 3: return Mirroring::Horizontal;
        default: return Mirroring::MapperControlled;
        }
    }

    NtSource ntSource(std::uint16_t /*addr*/) const override
    {
        if ((m_mode & 3) > 1) {
            return NtSource::Default;
        }
        return (m_mode & 1) != 0 ? NtSource::Ciram1 : NtSource::Ciram0;
    }

    std::uint8_t chrRead(std::uint16_t addr) const override
    {
        if (m_cart.chr.empty()) {
            if (m_cart.chrRam.empty()) {
                return 0;
            }
            const std::size_t bank = m_chrA13 & ((m_cart.chrRam.size() / 0x2000) - 1);
            return m_cart.chrRam[bank * 0x2000 + (addr & 0x1FFF)];
        }
        const std::size_t banks = m_cart.chr.size() / 0x2000;
        const std::size_t bank = m_chrA13 & (banks ? banks - 1 : 0);
        return m_cart.chr[(bank * 0x2000 + (addr & 0x1FFF))
            % m_cart.chr.size()];
    }

    void chrWrite(std::uint16_t addr, std::uint8_t value) override
    {
        if (m_cart.chr.empty() && !m_cart.chrRam.empty()) {
            const std::size_t bank =
                m_chrA13 & ((m_cart.chrRam.size() / 0x2000) - 1);
            m_cart.chrRam[bank * 0x2000 + (addr & 0x1FFF)] = value;
        }
    }

    void saveState(StateWriter& w) const override
    {
        w.begin("MAP ");
        w.w<std::uint16_t>(m_cart.info.mapperId);
        w.w(m_sel);
        w.w(m_chrA13);
        w.w(m_inner);
        w.w(m_mode);
        w.w(m_outer);
        // Former separate one-screen page byte, kept for layout stability.
        w.w(static_cast<std::uint8_t>(m_mode & 1));
        w.end();
    }

    void loadState(StateReader& r) override
    {
        (void)r.get<std::uint16_t>();
        m_sel = r.get<std::uint8_t>();
        m_chrA13 = r.get<std::uint8_t>();
        m_inner = r.get<std::uint8_t>();
        m_mode = r.get<std::uint8_t>();
        m_outer = r.get<std::uint8_t>();
        (void)r.get<std::uint8_t>(); // mirroring bit 0 lives in m_mode
    }

private:
    // Mirroring bit 0 has three writers: mode D0, and D4 of registers $00
    // and $01 while the mode is one-screen; in H/V modes D4 is ignored
    // (nesdev "Action 53 mapper"; test28 0.04's automated CHR bank test).
    void writeMirroringBit(std::uint8_t value)
    {
        if ((m_mode & 2) == 0) {
            m_mode = static_cast<std::uint8_t>((m_mode & ~1) | ((value >> 4) & 1));
        }
    }

    // PRG bank calculation, a direct port of the reference Python on
    // nesdev (Action_53_mapper/Reference_implementations) — verified by
    // the wiki's test vectors and by test28's automated test of every
    // mode/outer/inner combination.
    std::uint8_t prgByte(std::uint16_t addr) const
    {
        const std::uint32_t banks =
            static_cast<std::uint32_t>(m_cart.prg.size() / 0x4000);
        const unsigned cpuA14 = (addr >> 14) & 1;

        unsigned outer = static_cast<unsigned>(m_outer) << 1; // 16K bank units
        unsigned mode = m_mode >> 2;   // discard mirroring bits

        static constexpr unsigned kMasks[4] = {0x01, 0x03, 0x07, 0x0F};

        // In UNROM modes (10/11), a fixed-bank access (mode bit 0 ==
        // cpuA14) temporarily switches to 32K/NROM behavior.
        if (((mode ^ cpuA14) & 0x03) == 0x02) {
            mode = 0;
        }
        unsigned bank = m_inner;
        if ((mode & 0x02) == 0) {
            // 32K switched mode: cpuA14 becomes the bank's low bit.
            bank = (static_cast<unsigned>(m_inner) << 1) | cpuA14;
        }
        const unsigned mask = kMasks[(mode >> 2) & 3];
        const unsigned idx =
            ((bank & mask) | (outer & ~mask)) % banks;
        return m_cart.prg[idx * 0x4000 + (addr & 0x3FFF)];
    }

    std::uint8_t m_sel = 0x01; // power-up: user register 1 (inner bank)
    std::uint8_t m_chrA13 = 0;
    std::uint8_t m_inner = 0;
    // Power-up: PRG mode 3 (UNROM style, last bank fixed at $C000) with the
    // outer bank at $FF, so the last bank is visible at $C000-$FFFF and the
    // reset vector is readable. Hardware guarantees this; test28 checks
    // it. Mirroring bits 0-1 = 0 (one-screen, low page).
    std::uint8_t m_mode = 0x0C;
    std::uint8_t m_outer = 0xFF;
};

} // namespace

std::unique_ptr<Mapper> makeAction53(Cartridge& cart)
{
    return std::make_unique<Action53>(cart);
}

} // namespace cutenes
