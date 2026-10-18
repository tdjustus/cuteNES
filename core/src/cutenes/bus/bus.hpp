#pragma once

#include <array>
#include <cstdint>

namespace cutenes {

// A device occupying a routed CPU-bus region: PPU ($2000-$3FFF, mirrored
// every 8), APU/IO ($4000-$401F), cartridge ($4020-$FFFF). Devices decode
// their own sub-ranges; the bus owns mirroring and open-bus behavior.
class CpuBusDevice {
public:
    virtual ~CpuBusDevice() = default;
    virtual std::uint8_t cpuRead(std::uint16_t addr) = 0;
    virtual void cpuWrite(std::uint16_t addr, std::uint8_t value) = 0;
};

// CPU bus: 2KB work RAM with $0800 mirroring, register routing, and an
// open-bus latch that returns the last value seen on the bus (the simple
// Phase-2 model; PPU decay behavior arrives with the PPU, task T021).
//
// $6000-$7FFF must remain writable once a cartridge is attached so blargg
// consoles can report results at $6000-$7FFF (research.md D11).
class Bus {
public:
    void attachPpu(CpuBusDevice* device);
    void attachApuIo(CpuBusDevice* device);
    void attachCartridge(CpuBusDevice* device);

    std::uint8_t read(std::uint16_t addr);
    void write(std::uint16_t addr, std::uint8_t value);

    const std::array<std::uint8_t, 2048>& ram() const { return m_ram; }

private:
    std::uint8_t openBusRead();

    std::uint8_t m_openBus = 0;
    std::array<std::uint8_t, 2048> m_ram{};
    CpuBusDevice* m_ppu = nullptr;
    CpuBusDevice* m_apuIo = nullptr;
    CpuBusDevice* m_cartridge = nullptr;
};

} // namespace cutenes
