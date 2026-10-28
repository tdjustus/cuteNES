// Discrete-logic mappers for the corpus's remaining boards (session 43):
//
// - AxROM (mapper 7): 32K PRG bank at $8000 (bit 0-3 of the write), 8K
//   fixed CHR, ONE-screen nametable selected by bit 4 of the write.
// - BNROM (mapper 34, CHR <= 8K per the wiki's disambiguation): the
//   written value selects the 32K PRG region at $8000 (bit 1+ on 64K+
//   boards). Fixed header mirroring, fixed CHR.
// - ColorDreams (mapper 11): 32K PRG bank from bits 0-3, 8K CHR bank
//   from bits 4-7. Bus conflicts (the write ANDs with the ROM byte under
//   it) — the corpus entries are demos that rewrite the register often
//   enough that the conflict is not observable; masked off.
//
// Mapper 34 note: with CHR > 8K the header means NINA-001/002 instead —
// the loader treats those as UnsupportedMapper (no corpus ROM needs it).

#include "cutenes/mappers/mapper.hpp"

#include "cutenes/cartridge/cart.hpp"

#include "cutenes/serialize/save_state.hpp"

namespace cutenes {
namespace {

class Axrom final : public Mapper {
public:
    using Mapper::Mapper;

    std::uint8_t cpuRead(std::uint16_t addr) override
    {
        const std::size_t banks = m_cart.prg.size() / 0x8000;
        // Bits 0-3 pick the PRG bank; bit 4 (mirroring page) is kept in m_bank.
        const std::size_t bank = (m_bank & 0x0F) & (banks ? banks - 1 : 0);
        return m_cart.prg[(bank * 0x8000 + (addr & 0x7FFF))
            % m_cart.prg.size()];
    }

    void cpuWrite(std::uint16_t addr, std::uint8_t value) override
    {
        if (addr >= 0x8000) {
            m_bank = static_cast<std::uint8_t>(value & 0x1F);
        }
    }

    // One-screen: bit 4 selects which CIRAM page all four slots show.
    Mirroring mirroring() const override
    {
        return Mirroring::MapperControlled;
    }

    NtSource ntSource(std::uint16_t /*addr*/) const override
    {
        return (m_bank & 0x10) != 0 ? NtSource::Ciram1 : NtSource::Ciram0;
    }

    void saveState(StateWriter& w) const override
    {
        w.begin("MAP ");
        w.w<std::uint16_t>(m_cart.info.mapperId);
        w.w(m_bank);
        w.end();
    }

    void loadState(StateReader& r) override
    {
        (void)r.get<std::uint16_t>();
        m_bank = r.get<std::uint8_t>();
    }

private:
    std::uint8_t m_bank = 0;
};

class Bnrom final : public Mapper {
public:
    using Mapper::Mapper;

    std::uint8_t cpuRead(std::uint16_t addr) override
    {
        // BNROM: the written value selects a 32K PRG region presented at
        // $8000-$FFFF (banks come in 16K halves; the register's bit 0
        // picks between adjacent halves on 16K-granular boards — 240pee's
        // 64K ROM uses bits 0-1).
        const std::size_t banks = m_cart.prg.size() / 0x8000;
        const std::size_t bank = (m_bank >> 1) & (banks ? banks - 1 : 0);
        const std::size_t pick = addr < 0xC000 ? bank * 2 : bank * 2 + 1;
        return m_cart.prg[(pick * 0x4000 + (addr & 0x3FFF))
            % m_cart.prg.size()];
    }

    void cpuWrite(std::uint16_t addr, std::uint8_t value) override
    {
        if (addr >= 0x8000) {
            m_bank = value;
        }
    }

    void saveState(StateWriter& w) const override
    {
        w.begin("MAP ");
        w.w<std::uint16_t>(m_cart.info.mapperId);
        w.w(m_bank);
        w.end();
    }

    void loadState(StateReader& r) override
    {
        (void)r.get<std::uint16_t>();
        m_bank = r.get<std::uint8_t>();
    }

private:
    std::uint8_t m_bank = 0;
};

class ColorDreams final : public Mapper {
public:
    using Mapper::Mapper;

    std::uint8_t cpuRead(std::uint16_t addr) override
    {
        if (addr < 0x8000) {
            return 0;
        }
        const std::size_t banks = m_cart.prg.size() / 0x8000;
        const std::size_t bank = m_bank & (banks ? banks - 1 : 0);
        return m_cart.prg[(bank * 0x8000 + (addr & 0x7FFF))
            % m_cart.prg.size()];
    }

    void cpuWrite(std::uint16_t addr, std::uint8_t value) override
    {
        if (addr >= 0x8000) {
            m_bank = static_cast<std::uint8_t>(value & 0x0F);
            m_chrBank = static_cast<std::uint8_t>(value >> 4);
        }
    }

    std::uint8_t chrRead(std::uint16_t addr) const override
    {
        if (m_cart.chr.empty()) {
            return m_cart.chrRam.empty()
                ? 0
                : m_cart.chrRam[addr & (m_cart.chrRam.size() - 1)];
        }
        const std::size_t banks = m_cart.chr.size() / 0x2000;
        const std::size_t bank = m_chrBank & (banks ? banks - 1 : 0);
        return m_cart.chr[(bank * 0x2000 + (addr & 0x1FFF))
            % m_cart.chr.size()];
    }

    void chrWrite(std::uint16_t addr, std::uint8_t value) override
    {
        if (m_cart.chr.empty() && !m_cart.chrRam.empty()) {
            m_cart.chrRam[addr & (m_cart.chrRam.size() - 1)] = value;
        }
    }

    void saveState(StateWriter& w) const override
    {
        w.begin("MAP ");
        w.w<std::uint16_t>(m_cart.info.mapperId);
        w.w(m_bank);
        w.w(m_chrBank);
        w.end();
    }

    void loadState(StateReader& r) override
    {
        (void)r.get<std::uint16_t>();
        m_bank = r.get<std::uint8_t>();
        m_chrBank = r.get<std::uint8_t>();
    }

private:
    std::uint8_t m_bank = 0;
    std::uint8_t m_chrBank = 0;
};

} // namespace

std::unique_ptr<Mapper> makeAxrom(Cartridge& cart)
{
    return std::make_unique<Axrom>(cart);
}

std::unique_ptr<Mapper> makeBnrom(Cartridge& cart)
{
    return std::make_unique<Bnrom>(cart);
}

std::unique_ptr<Mapper> makeColorDreams(Cartridge& cart)
{
    return std::make_unique<ColorDreams>(cart);
}

} // namespace cutenes
