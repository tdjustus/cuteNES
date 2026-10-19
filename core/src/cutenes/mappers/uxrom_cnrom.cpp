// UxROM (mapper 2) and CNROM (mapper 3) — task T054/T055.
//
// UxROM: 16K switchable bank at $8000 + fixed last bank at $C000, bus
// conflicts on some boards (the written value ANDs with the ROM byte;
// UxROM boards typically don't conflict — the AND/NOMATCH variants land
// with tier-2 verification). CHR-RAM 8K.
// CNROM: fixed PRG (NROM-style), 8K CHR bank switch (2 or 4 bits), no bus
// conflicts.

#include "cutenes/mappers/mapper.hpp"

#include "cutenes/cartridge/cart.hpp"
#include "cutenes/serialize/save_state.hpp"

namespace cutenes {
namespace {

class Uxrom final : public Mapper {
public:
    using Mapper::Mapper;

    std::uint8_t cpuRead(std::uint16_t addr) override
    {
        const std::size_t banks = m_cart.prg.size() / 0x4000;
        const std::size_t bank =
            addr < 0xC000 ? (m_bank & (banks - 1)) : banks - 1;
        return m_cart.prg[(bank * 0x4000 + (addr & 0x3FFF))
            % m_cart.prg.size()];
    }

    void cpuWrite(std::uint16_t addr, std::uint8_t value) override
    {
        if (addr >= 0x8000) {
            m_bank = static_cast<std::uint8_t>(value & 0x0F);
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

class Cnrom final : public Mapper {
public:
    using Mapper::Mapper;

    std::uint8_t cpuRead(std::uint16_t addr) override
    {
        if (addr < 0x8000) {
            return m_cart.prgRam.empty() ? 0
                : m_cart.prgRam[addr % m_cart.prgRam.size()];
        }
        return m_cart.prg[(addr - 0x8000) % m_cart.prg.size()];
    }

    void cpuWrite(std::uint16_t addr, std::uint8_t value) override
    {
        if (addr < 0x8000 && !m_cart.prgRam.empty()) {
            m_cart.prgRam[addr % m_cart.prgRam.size()] = value;
        } else if (addr >= 0x8000) {
            m_bank = static_cast<std::uint8_t>(value & 0x03);
        }
    }

    std::uint8_t chrRead(std::uint16_t addr) const override
    {
        if (m_cart.chr.empty()) { // CHR-RAM
            return m_cart.chrRam.empty() ? 0
                : m_cart.chrRam[addr & (m_cart.chrRam.size() - 1)];
        }
        const std::size_t bankBytes = 8192;
        return m_cart.chr[(static_cast<std::size_t>(m_bank) * bankBytes
            + (addr & 0x1FFF))
            % m_cart.chr.size()];
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

} // namespace

std::unique_ptr<Mapper> makeUxrom(Cartridge& cart)
{
    return std::make_unique<Uxrom>(cart);
}

std::unique_ptr<Mapper> makeCnrom(Cartridge& cart)
{
    return std::make_unique<Cnrom>(cart);
}

} // namespace cutenes
