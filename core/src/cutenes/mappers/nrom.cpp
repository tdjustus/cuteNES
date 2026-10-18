#include "cutenes/mappers/mapper.hpp"

#include "cutenes/cartridge/cart.hpp"

namespace cutenes {

std::unique_ptr<Mapper> makeMmc1(Cartridge& cart);

namespace {

// NROM (mapper 0): fixed PRG (16K mirrored or 32K), fixed CHR, header
// mirroring, PRG-RAM at $6000-$7FFF.
class NromMapper final : public Mapper {
public:
    using Mapper::Mapper;

    std::uint8_t cpuRead(std::uint16_t addr) override
    {
        const auto& prg = m_cart.prg;
        // NROM-128 (16K) mirrors across both CPU windows; NROM-256 maps
        // linearly.
        const std::size_t window = prg.size() == 16384 ? 0x3FFF : 0x7FFF;
        return prg[(addr - 0x8000) & window];
    }

    void cpuWrite(std::uint16_t addr, std::uint8_t /*value*/) override
    {
        // NROM has no writeable program space; PRG-RAM writes are handled by
        // the Cartridge before reaching the mapper (see cart.cpp).
        (void)addr;
    }
};

} // namespace

std::unique_ptr<Mapper> createMapper(std::uint16_t mapperId, Cartridge& cart)
{
    switch (mapperId) {
    case 0:
        return std::make_unique<NromMapper>(cart);
    case 1:
        return makeMmc1(cart);
    default:
        // Mappers 2-4 land with the remaining US6 tasks (T054-T056).
        return nullptr;
    }
}

} // namespace cutenes
