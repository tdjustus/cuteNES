// Cartridge bus-device behavior: PRG-RAM window + mapper dispatch.

#include "cutenes/cartridge/cart.hpp"

#include "cutenes/mappers/mapper.hpp"

namespace cutenes {

std::uint8_t Cartridge::cpuRead(std::uint16_t addr)
{
    if (addr < 0x8000) {
        // $6000-$7FFF PRG-RAM (kept writable for blargg reporting, D11);
        // below $6000 the bus never routes here.
        if (addr < 0x6000 || prgRam.empty()) {
            return 0;
        }
        return prgRam[addr % prgRam.size()];
    }
    return mapper ? mapper->cpuRead(addr) : 0;
}

void Cartridge::cpuWrite(std::uint16_t addr, std::uint8_t value)
{
    if (addr < 0x8000) {
        if (addr >= 0x6000 && !prgRam.empty()) {
            prgRam[addr % prgRam.size()] = value;
        }
        return;
    }
    // Mapper register writes (none for NROM).
    if (mapper) {
        mapper->cpuWrite(addr, value);
    }
}

std::uint8_t Cartridge::chrRead(std::uint16_t addr) const
{
    return mapper ? const_cast<Mapper*>(mapper.get())->chrRead(addr) : 0;
}

void Cartridge::chrWrite(std::uint16_t addr, std::uint8_t value)
{
    if (mapper) {
        mapper->chrWrite(addr, value);
    }
}

Mirroring Cartridge::mirroring() const
{
    return mapper ? mapper->mirroring() : info.mirroring;
}

} // namespace cutenes
