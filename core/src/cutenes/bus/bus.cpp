#include "cutenes/bus/bus.hpp"

#include "cutenes/serialize/save_state.hpp"

namespace cutenes {

void Bus::saveState(StateWriter& w) const
{
    w.begin("RAM ");
    w.wBytes(m_ram.data(), m_ram.size());
    w.w(m_openBus);
    w.end();
}

void Bus::loadState(StateReader& r)
{
    r.getBytes(m_ram.data(), m_ram.size());
    m_openBus = r.get<std::uint8_t>();
}

void Bus::attachPpu(CpuBusDevice* device) { m_ppu = device; }
void Bus::attachApuIo(CpuBusDevice* device) { m_apuIo = device; }
void Bus::attachCartridge(CpuBusDevice* device) { m_cartridge = device; }

int g_bus4014 = 0;

std::uint8_t Bus::read(std::uint16_t addr)
{
    std::uint8_t value = m_openBus;

    if (addr < 0x2000) {
        // Work RAM mirrored every $0800.
        value = m_ram[addr & 0x07FF];
    } else if (addr < 0x4000) {
        // PPU registers mirrored every 8 bytes.
        if (m_ppu != nullptr) {
            value = m_ppu->cpuRead(static_cast<std::uint16_t>(addr & 0x2007));
        }
    } else if (addr < 0x4020) {
        if (m_apuIo != nullptr) {
            value = m_apuIo->cpuRead(addr);
        }
    } else if (addr < 0x6000) {
        // $4020-$5FFF: mapper decode space (MMC5's $5000-$5FFF registers,
        // ExRAM). Boards that do not claim the range leave open bus.
        if (m_cartridge != nullptr && m_cartridge->decodesLowCpuSpace()) {
            value = m_cartridge->cpuRead(addr);
        }
    } else {
        // 0x6000-0xFFFF: PRG RAM (blargg reporting) + program ROM.
        if (m_cartridge != nullptr) {
            value = applyGameGenie(addr, m_cartridge->cpuRead(addr));
        }
    }

    m_openBus = value;
    return value;
}

std::uint8_t Bus::applyGameGenie(std::uint16_t addr, std::uint8_t cartridge) const
{
    if (addr < 0x8000) {
        return cartridge;
    }
    for (const GameGenieCode& code : m_genie) {
        if (code.address == addr && (!code.compare || *code.compare == cartridge)) {
            return code.value;
        }
    }
    return cartridge;
}

std::uint8_t Bus::peek(std::uint16_t addr) const
{
    if (addr < 0x2000) {
        return m_ram[addr & 0x07FF];
    }
    if (addr >= 0x6000) {
        return m_cartridge != nullptr ? applyGameGenie(addr, m_cartridge->cpuRead(addr)) : 0;
    }
    // Register regions: peeking must not disturb device state. The golden
    // nestest trace previews this space with the bus-high value.
    return addr >= 0x4000 ? 0xFF : 0;
}

void Bus::write(std::uint16_t addr, std::uint8_t value)
{
    m_openBus = value;

    if (addr < 0x2000) {
        m_ram[addr & 0x07FF] = value;
    } else if (addr < 0x4000) {
        if (m_ppu != nullptr) {
            m_ppu->cpuWrite(static_cast<std::uint16_t>(addr & 0x2007), value);
        }
    } else if (addr < 0x4020) {
        if (m_apuIo != nullptr) {
            m_apuIo->cpuWrite(addr, value);
        }
    } else if (addr < 0x6000) {
        // $4020-$5FFF: mapper decode space (MMC5 registers, Action 53's
        // write-only select); otherwise unmapped.
        if (m_cartridge != nullptr
            && (m_cartridge->decodesLowCpuSpace()
                || m_cartridge->decodesLowCpuWrites())) {
            m_cartridge->cpuWrite(addr, value);
        }
    } else if (m_cartridge != nullptr) {
        m_cartridge->cpuWrite(addr, value);
    }
}

} // namespace cutenes
