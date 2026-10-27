// Konami VRC2a (iNES mapper 22) — session 44.
//
// The corpus's last unsupported mapper (m22chrbankingtest). Board
// 351618 wires the ASIC's two register-select lines to CPU A1/A0
// (swapped relative to VRC2b), so the sub-register within each $1000
// band decodes as $x000, $x002, $x001, $x003 (nesdev "VRC2 and VRC4 /
// Variations"). Registers implemented:
//   * PRG Select 0 ($8xxx): 8K bank at $8000-$9FFF (5 bits).
//   * Nametable arrangement ($9xxx): bit 0, 0 = vertical, 1 = horizontal
//     (VRC2 has no one-screen mode; bit 1 ignored).
//   * PRG Select 1 ($Axxx): 8K bank at $A000-$BFFF.
//   * CHR Select 0-7 ($B000-$E003): two-write nibble registers, low 4
//     bits then high 4 (VRC2 has 4 high bits, 256K CHR max).
//   * $C000-$FFFF is fixed to the last 16K of PRG.
// VRC2a additionally swaps the CHR bank's A0/A1 lines: the effective
// 1K bank is the register value with bits 0 and 1 exchanged.
// Not implemented (no corpus coverage): VRC2's $6000-$6FFF Microwire
// latch (EEPROM games) and the whole VRC4 (IRQ, $C000 bank) — mappers
// 21/23/25 stay refused until a ROM needs them.

#include "cutenes/mappers/mapper.hpp"

#include "cutenes/cartridge/cart.hpp"

#include "cutenes/serialize/save_state.hpp"

#include <array>
#include <cstdint>

namespace cutenes {
namespace {

class Vrc2a final : public Mapper {
public:
    using Mapper::Mapper;

    std::uint8_t cpuRead(std::uint16_t addr) override
    {
        // $8000-$9FFF / $A000-$BFFF banked, $C000-$FFFF fixed to the
        // last 16K (VRC2 has no swap mode).
        if (addr < 0xA000) {
            return prgRead(m_prg0, addr);
        }
        if (addr < 0xC000) {
            return prgRead(m_prg1, addr);
        }
        const std::size_t banks = m_cart.prg.size() / 0x4000;
        const std::size_t last = banks ? banks - 1 : 0;
        return m_cart.prg.empty()
            ? 0
            : m_cart.prg[(last * 0x4000 + (addr & 0x3FFF))
                % m_cart.prg.size()];
    }

    void cpuWrite(std::uint16_t addr, std::uint8_t value) override
    {
        if (addr < 0x8000) {
            return;
        }
        // Sub-register select: the board's swapped lines make the ASIC
        // address (CPU_A0*2 + CPU_A1) — CPU $x000, $x002, $x001, $x003.
        const int sub = ((addr & 1) << 1) | ((addr >> 1) & 1);
        switch (addr >> 12) {
        case 0x8: // PRG Select 0
            m_prg0 = static_cast<std::uint8_t>(value & 0x1F);
            break;
        case 0x9: // Nametable arrangement (VRC2: vertical/horizontal)
            m_mirroringBit = static_cast<std::uint8_t>(value & 1);
            break;
        case 0xA: // PRG Select 1
            m_prg1 = static_cast<std::uint8_t>(value & 0x1F);
            break;
        case 0xB: // CHR Select 0/1
        case 0xC: // CHR Select 2/3
        case 0xD: // CHR Select 4/5
        case 0xE: // CHR Select 6/7
            {
                const int pair = (addr >> 12) - 0xB;
                const int reg = pair * 2 + (sub >> 1);
                if (sub == 0 || sub == 2) {
                    // Low-nibble write: latch, keep the high bits.
                    m_chrRegs[static_cast<std::size_t>(reg)] =
                        static_cast<std::uint8_t>(
                            (m_chrRegs[static_cast<std::size_t>(reg)]
                                & 0xF0)
                            | (value & 0x0F));
                } else {
                    // High-nibble write: VRC2 has 4 high bits.
                    m_chrRegs[static_cast<std::size_t>(reg)] =
                        static_cast<std::uint8_t>(
                            (m_chrRegs[static_cast<std::size_t>(reg)]
                                & 0x0F)
                            | ((value & 0x0F) << 4));
                }
            }
            break;
        default: // $F: VRC4 IRQ range — VRC2 has nothing there
            break;
        }
    }

    std::uint8_t chrRead(std::uint16_t addr) const override
    {
        const std::size_t index = (addr >> 10) & 7;
        // VRC2a: CHR A0/A1 lines are swapped on the PCB.
        const std::uint8_t v = m_chrRegs[index];
        const std::size_t bank = static_cast<std::size_t>(
            (v & 0xFC) | ((v & 1) << 1) | ((v >> 1) & 1));
        if (m_cart.chr.empty()) {
            return 0;
        }
        return m_cart.chr[(bank * 0x400 + (addr & 0x3FF)) % m_cart.chr.size()];
    }

    // The register controls the arrangement (vertical/horizontal).
    Mirroring mirroring() const override
    {
        return m_mirroringBit != 0 ? Mirroring::Horizontal
                                   : Mirroring::Vertical;
    }

    void saveState(StateWriter& w) const override
    {
        w.begin("MAP ");
        w.w<std::uint16_t>(m_cart.info.mapperId);
        w.w(m_prg0);
        w.w(m_prg1);
        w.w(m_mirroringBit);
        for (const std::uint8_t v : m_chrRegs) {
            w.w(v);
        }
        w.end();
    }

    void loadState(StateReader& r) override
    {
        (void)r.get<std::uint16_t>(); // mapperId
        m_prg0 = r.get<std::uint8_t>();
        m_prg1 = r.get<std::uint8_t>();
        m_mirroringBit = r.get<std::uint8_t>();
        for (auto& v : m_chrRegs) {
            v = r.get<std::uint8_t>();
        }
    }

private:
    std::uint8_t prgRead(std::uint8_t bankReg, std::uint16_t addr) const
    {
        const std::size_t banks = m_cart.prg.size() / 0x2000;
        const std::size_t bank =
            static_cast<std::size_t>(bankReg) % (banks ? banks : 1);
        return m_cart.prg.empty()
            ? 0
            : m_cart.prg[(bank * 0x2000 + (addr & 0x1FFF))
                % m_cart.prg.size()];
    }

    std::uint8_t m_prg0 = 0;
    std::uint8_t m_prg1 = 1;
    std::uint8_t m_mirroringBit = 0;
    std::array<std::uint8_t, 8> m_chrRegs{};
};

} // namespace

std::unique_ptr<Mapper> makeVrc2a(Cartridge& cart)
{
    return std::make_unique<Vrc2a>(cart);
}

} // namespace cutenes
