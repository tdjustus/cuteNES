#pragma once

#include <cutenes/game_genie.hpp>

#include <array>
#include <cstdint>
#include <vector>

namespace cutenes {

class StateReader;
class StateWriter;

// A device occupying a routed CPU-bus region: PPU ($2000-$3FFF, mirrored
// every 8), APU/IO ($4000-$401F), cartridge ($4020-$FFFF). Devices decode
// their own sub-ranges; the bus owns mirroring and open-bus behavior.
class CpuBusDevice {
public:
    virtual ~CpuBusDevice() = default;
    virtual std::uint8_t cpuRead(std::uint16_t addr) = 0;
    virtual void cpuWrite(std::uint16_t addr, std::uint8_t value) = 0;
    // $4020-$5FFF decode claim (MMC5 registers/ExRAM). Devices that do not
    // override leave the range open bus, as on a stock NES.
    virtual bool decodesLowCpuSpace() const { return false; }
    // $4020-$5FFF write-only registers (Action 53's $5000 select) that do not
    // claim reads or the rest of the low space.
    virtual bool decodesLowCpuWrites() const { return false; }
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

    // Side-effect-free read for disassembly/tracing: no open-bus latch
    // update, no register side effects (stateful regions return 0).
    std::uint8_t peek(std::uint16_t addr) const;
    void write(std::uint16_t addr, std::uint8_t value);

    const std::array<std::uint8_t, 2048>& ram() const { return m_ram; }

    // Game Genie: reads in $8000-$FFFF at a code's address are substituted
    // (Machine::setGameGenieCodes).
    void setGameGenieCodes(std::vector<GameGenieCode> codes) { m_genie = std::move(codes); }

    // The CPU data-bus open-bus latch: the last byte driven on the bus.
    // Read drivers with no register behind them return this (write-only
    // APU ports, $4018-$401F — blargg cpu_exec_space).
    std::uint8_t openBusRead() const { return m_openBus; }

    // Save-state section "RAM " (task T049).
    void saveState(class StateWriter& w) const;
    void loadState(StateReader& r);

private:
    std::uint8_t m_openBus = 0;
    std::array<std::uint8_t, 2048> m_ram{};
    CpuBusDevice* m_ppu = nullptr;
    CpuBusDevice* m_apuIo = nullptr;
    CpuBusDevice* m_cartridge = nullptr;
    std::vector<GameGenieCode> m_genie;
    std::uint8_t applyGameGenie(std::uint16_t addr, std::uint8_t cartridge) const;
};

} // namespace cutenes
