#include "cutenes/mappers/mapper.hpp"

#include "cutenes/cartridge/cart.hpp"

namespace cutenes {

std::uint8_t Mapper::chrRead(std::uint16_t addr) const
{
    // Shared default: ROM when present, RAM otherwise (CHR-RAM carts).
    const auto& chr = m_cart.chr;
    if (!chr.empty()) {
        return chr[addr % chr.size()];
    }
    if (!m_cart.chrRam.empty()) {
        return m_cart.chrRam[addr % m_cart.chrRam.size()];
    }
    return 0;
}

void Mapper::chrWrite(std::uint16_t addr, std::uint8_t value)
{
    // CHR-ROM is not writable; CHR-RAM is.
    if (m_cart.chr.empty() && !m_cart.chrRam.empty()) {
        m_cart.chrRam[addr % m_cart.chrRam.size()] = value;
    }
}

Mirroring Mapper::mirroring() const { return m_cart.info.mirroring; }

} // namespace cutenes
