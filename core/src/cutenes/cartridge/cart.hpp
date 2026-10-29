#pragma once

#include "cutenes/bus/bus.hpp"
#include "cutenes/mappers/mapper.hpp"

#include <cutenes/cutenes.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace cutenes {

class Scheduler;

// A loaded cartridge: parsed ROM data plus its mapper instance. Implements
// the CPU-bus cartridge slot; the PPU reaches pattern data via chrRead.
struct Cartridge final : CpuBusDevice {
    // Parse iNES 1.0 / NES 2.0 (task T018). Rejections name the exact
    // defect; UnsupportedMapper carries the mapper id in `detail`.
    static LoadResult load(const std::uint8_t* data, std::size_t size,
        Cartridge& out);

    std::uint8_t cpuRead(std::uint16_t addr) override;
    void cpuWrite(std::uint16_t addr, std::uint8_t value) override;
    bool decodesLowCpuSpace() const override
    {
        return ownsLowSpace(); // MMC5's $5000-$5FFF registers/ExRAM
    }
    bool decodesLowCpuWrites() const override
    {
        return mapper && mapper->decodesLowWrites(); // Action 53's $5000
    }

    // PPU bus ($0000-$1FFF pattern tables).
    std::uint8_t chrRead(std::uint16_t addr) const;
    void chrWrite(std::uint16_t addr, std::uint8_t value);
    // Rendering pattern fetch (sprite/background context) and raw storage
    // read for mapper-supplied absolute addresses.
    std::uint8_t chrFetch(std::uint16_t addr, bool sprite) const;
    std::uint8_t chrReadRaw(std::uint32_t addr) const;

    // Mapper-owned low CPU space / nametable hooks (MMC5).
    bool ownsLowSpace() const { return mapper && mapper->ownsLowSpace(); }
    Mapper::NtSource ntSource(std::uint16_t addr) const
    {
        return mapper ? mapper->ntSource(addr) : Mapper::NtSource::Default;
    }
    std::uint8_t ntRead(std::uint16_t addr) const
    {
        return mapper ? mapper->ntRead(addr) : 0;
    }
    void ntWrite(std::uint16_t addr, std::uint8_t value)
    {
        if (mapper) {
            mapper->ntWrite(addr, value);
        }
    }

    Mirroring mirroring() const;

    // A12-rise IRQ clocking (MMC3-class mappers; no-op otherwise).
    void clockIrq() { if (mapper) mapper->clockIrq(); }
    bool irqLine() const { return mapper && mapper->irqLine(); }

    // PPU integration hooks (MMC5; no-ops for other boards).
    void setPpuCtrl(std::uint8_t value)
    {
        if (mapper) {
            mapper->setPpuCtrl(value);
        }
    }
    void setPpuMask(std::uint8_t value)
    {
        if (mapper) {
            mapper->setPpuMask(value);
        }
    }
    bool bgFetch(Mapper::BgFetch& fetch) const
    {
        return mapper && mapper->bgFetch(fetch);
    }
    void onScanline(int line, bool rendering)
    {
        if (mapper) {
            mapper->onScanline(line, rendering);
        }
    }

    // Write-cycle context for mappers with consecutive-write rules.
    void attachScheduler(Scheduler& scheduler)
    {
        if (mapper) {
            mapper->attachScheduler(scheduler);
        }
    }

    CartridgeInfo info{};
    std::vector<std::uint8_t> prg;
    std::vector<std::uint8_t> chr;    // empty => CHR-RAM (8K default)
    std::vector<std::uint8_t> chrRam;
    std::vector<std::uint8_t> prgRam; // battery-backed when info.battery

    std::unique_ptr<Mapper> mapper;
};

} // namespace cutenes
