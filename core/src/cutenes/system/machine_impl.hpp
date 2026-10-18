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

// $4000-$401F bridge: APU registers, OAM DMA ($4014), controller ports
// (polling behavior arrives with task T044).
struct ApuIoBridge final : CpuBusDevice {
    Scheduler* sched = nullptr;
    Bus* bus = nullptr;
    Ppu* ppu = nullptr;
    Apu* apu = nullptr;
    std::uint8_t strobe = 0;

    std::uint8_t cpuRead(std::uint16_t addr) override
    {
        if (addr == 0x4015) {
            return apu->registerRead(addr);
        }
        if (addr == 0x4016 || addr == 0x4017) {
            return static_cast<std::uint8_t>(0x40); // open-bus high bits
        }
        return 0;
    }

    void cpuWrite(std::uint16_t addr, std::uint8_t value) override
    {
        if (addr == 0x4014) {
            oamDma(value);
        } else if (addr == 0x4016) {
            strobe = static_cast<std::uint8_t>(value & 1); // shared strobe
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
