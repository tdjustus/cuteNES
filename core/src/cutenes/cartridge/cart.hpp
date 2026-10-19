#pragma once

#include "cutenes/bus/bus.hpp"
#include "cutenes/mappers/mapper.hpp"

#include <cutenes/cutenes.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace cutenes {

// A loaded cartridge: parsed ROM data plus its mapper instance. Implements
// the CPU-bus cartridge slot; the PPU reaches pattern data via chrRead.
struct Cartridge final : CpuBusDevice {
    // Parse iNES 1.0 / NES 2.0 (task T018). Rejections name the exact
    // defect; UnsupportedMapper carries the mapper id in `detail`.
    static LoadResult load(const std::uint8_t* data, std::size_t size,
        Cartridge& out);

    std::uint8_t cpuRead(std::uint16_t addr) override;
    void cpuWrite(std::uint16_t addr, std::uint8_t value) override;

    // PPU bus ($0000-$1FFF pattern tables).
    std::uint8_t chrRead(std::uint16_t addr) const;
    void chrWrite(std::uint16_t addr, std::uint8_t value);

    Mirroring mirroring() const;

    // A12-rise IRQ clocking (MMC3-class mappers; no-op otherwise).
    void clockIrq() { if (mapper) mapper->clockIrq(); }
    bool irqLine() const { return mapper && mapper->irqLine(); }

    CartridgeInfo info{};
    std::vector<std::uint8_t> prg;
    std::vector<std::uint8_t> chr;    // empty => CHR-RAM (8K default)
    std::vector<std::uint8_t> chrRam;
    std::vector<std::uint8_t> prgRam; // battery-backed when info.battery

    std::unique_ptr<Mapper> mapper;
};

} // namespace cutenes
