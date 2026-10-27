#include "cutenes/mappers/mapper.hpp"

#include "cutenes/cartridge/cart.hpp"

namespace cutenes {

std::unique_ptr<Mapper> makeMmc1(Cartridge& cart);
std::unique_ptr<Mapper> makeUxrom(Cartridge& cart);
std::unique_ptr<Mapper> makeCnrom(Cartridge& cart);
std::unique_ptr<Mapper> makeAxrom(Cartridge& cart);
std::unique_ptr<Mapper> makeBnrom(Cartridge& cart);
std::unique_ptr<Mapper> makeColorDreams(Cartridge& cart);
std::unique_ptr<Mapper> makeAction53(Cartridge& cart);
std::unique_ptr<Mapper> makeMmc3(Cartridge& cart);

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
    case 155: { // MMC1A revision (nesdev MMC1, "ASIC Revisions")
        auto mmc1 = makeMmc1(cart);
        mmc1->setRevisionA(true);
        return mmc1;
    }
    case 2:
        return makeUxrom(cart);
    case 3:
        return makeCnrom(cart);
    case 7:
        return makeAxrom(cart);
    case 11:
        return makeColorDreams(cart);
    case 28:
        return makeAction53(cart);
    case 34: // BNROM only when CHR <= 8K (else NINA, unsupported)
        return cart.chr.size() <= 0x2000 ? makeBnrom(cart) : nullptr;
    case 4:
        return makeMmc3(cart);
    default:
        return nullptr;
    }
}

} // namespace cutenes
