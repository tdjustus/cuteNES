#pragma once

#include "cutenes/bus/bus.hpp"

#include <cutenes/cutenes.hpp>

#include <cstdint>
#include <memory>

// cart.hpp includes this header (its unique_ptr<Mapper> needs the complete
// type), so Cartridge can only be forward-declared here.

namespace cutenes {

class StateReader;
class StateWriter;

struct Cartridge;

// Board/mapper behavior: CPU-bus cartridge decoding (bank switching), PPU
// pattern-bus decoding, and mirroring control (research.md D1 component
// design). Save-state serialization hooks arrive with US5 (task T049).
class Mapper : public CpuBusDevice {
public:
    explicit Mapper(Cartridge& cart)
        : m_cart(cart)
    {
    }
    ~Mapper() override = default;

    // PPU pattern bus $0000-$1FFF; default: direct CHR/CHR-RAM.
    virtual std::uint8_t chrRead(std::uint16_t addr) const;
    virtual void chrWrite(std::uint16_t addr, std::uint8_t value);

    // Nametable mirroring for the PPU; header value unless mapper-controlled.
    virtual Mirroring mirroring() const;

    // Save-state section "MAP " (task T049).
    virtual void saveState(class StateWriter& w) const;
    virtual void loadState(StateReader& r);

protected:
    Cartridge& m_cart;
};

// Factory: returns nullptr for mapper ids without an implementation yet
// (mapper 0 here; 1-4 land with US6, tasks T053-T056).
std::unique_ptr<Mapper> createMapper(std::uint16_t mapperId,
    Cartridge& cart);

} // namespace cutenes
