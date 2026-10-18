#pragma once

#include "cutenes/apu/apu.hpp"
#include "cutenes/bus/bus.hpp"
#include "cutenes/cartridge/cart.hpp"
#include "cutenes/cpu/cpu.hpp"
#include "cutenes/mappers/mapper.hpp"
#include "cutenes/ppu/ppu.hpp"
#include "cutenes/system/region.hpp"
#include "cutenes/system/scheduler.hpp"

#include <cutenes/cutenes.hpp>

#include <array>
#include <cstdint>
#include <memory>

namespace cutenes {

// $4000-$401F bridge: APU registers, OAM DMA ($4014), and the two
// controller ports with authentic serial polling (task T044).
struct ApuIoBridge final : CpuBusDevice {
    Scheduler* sched = nullptr;
    Bus* bus = nullptr;
    Ppu* ppu = nullptr;
    Apu* apu = nullptr;
    ButtonSet* controllers = nullptr; // machine-held button state [2]
    std::uint8_t strobe = 0;
    std::uint8_t shiftPos[2] = {0, 0};

    std::uint8_t cpuRead(std::uint16_t addr) override
    {
        if (addr == 0x4015) {
            return apu->registerRead(addr);
        }
        if (addr == 0x4016 || addr == 0x4017) {
            // Serial controller read: bit 0 carries the current button in
            // read order A,B,Select,Start,Up,Down,Left,Right; high bits are
            // open bus (0x40 typical).
            const int port = addr == 0x4017 ? 1 : 0;
            std::uint8_t bit = 0;
            if (controllers != nullptr) {
                const ButtonSet& set = controllers[port];
                // Read order (first to last); Button enum stores the same
                // bits right-to-left.
                static constexpr Button kOrder[8] = {
                    Button::A, Button::B, Button::Select, Button::Start,
                    Button::Up, Button::Down, Button::Left, Button::Right,
                };
                const std::uint8_t pos = shiftPos[port];
                if (strobe != 0) {
                    bit = static_cast<std::uint8_t>(
                        set[static_cast<std::size_t>(Button::A)]);
                } else if (pos < 8) {
                    bit = static_cast<std::uint8_t>(
                        set[static_cast<std::size_t>(kOrder[pos])]);
                    shiftPos[port] = static_cast<std::uint8_t>(pos + 1);
                } // after 8 reads: 0 (open bus low)
            }
            return static_cast<std::uint8_t>(0x40 | bit);
        }
        return 0;
    }

    void cpuWrite(std::uint16_t addr, std::uint8_t value) override
    {
        if (addr == 0x4014) {
            oamDma(value);
        } else if (addr == 0x4016) {
            const std::uint8_t next = static_cast<std::uint8_t>(value & 1);
            if (strobe != 0 && next == 0) {
                // Falling edge: latch state, restart the shift.
                shiftPos[0] = 0;
                shiftPos[1] = 0;
            }
            strobe = next;
        } else {
            // $4000-$4013, $4015, and $4017 (frame counter) are APU.
            apu->registerWrite(addr, value);
        }
    }

    // Sprite DMA: 513 cycles (+1 alignment when starting on an odd CPU
    // cycle) — halt, optional align, then 256 get/put pairs. Every cycle
    // advances both PPU and APU catch-up (task T037).
    void dmaCycle()
    {
        sched->advanceMaster(12);
        ppu->runTo(sched->now());
        apu->runTo(sched->now());
    }

    void oamDma(std::uint8_t page)
    {
        const std::uint16_t base = static_cast<std::uint16_t>(page << 8);
        dmaCycle();                              // halt cycle
        if ((sched->now() / 12) % 2 == 1) {       // align to a get cycle
            dmaCycle();
        }
        for (int i = 0; i < 256; ++i) {
            dmaCycle();                           // get cycle setup
            const std::uint8_t byte = bus->read(
                static_cast<std::uint16_t>(base + i));
            dmaCycle();                           // put cycle
            ppu->oamDmaWrite(byte);
        }
    }
};

// Private implementation detail of cutenes::Machine (pimpl). Components are
// constructed after the region resolves (load may select PAL), hence the
// unique_ptrs.
struct Machine::Impl {
    explicit Impl(const MachineConfig& config);

    void buildComponents(Region region);
    void deliverFrame();
    void runUntilFrameComplete();

    MachineConfig config;
    Region regionId = Region::Ntsc;
    const RegionProfile* profile = &regionProfile(Region::Ntsc);
    Scheduler scheduler{*profile};
    Bus bus;
    ApuIoBridge io;

    Cartridge cart;
    std::unique_ptr<Ppu> ppu;
    std::unique_ptr<Apu> apu;
    std::unique_ptr<Cpu> cpu;

    IVideoSink* videoSink = nullptr;
    std::array<std::uint32_t, 256 * 240> framebuffer{};

    RunState runState = RunState::NeedsRom;
    std::uint64_t frameCounter = 0;
    ButtonSet controllers[2]{};
};

} // namespace cutenes
