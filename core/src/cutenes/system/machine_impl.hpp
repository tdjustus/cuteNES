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
#include <vector>
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
                } else {
                    // After 8 shifts the official controller keeps
                    // returning 1s (nesdev Controllers; terminates
                    // blargg dmc_dma_during_read4's bit-count loop).
                    bit = 1;
                }
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

    // Sprite DMA (task T037, nesdev wiki DMA): halt, optional align, then
    // 256 get/put pairs. CPU cycles alternate get (read) / put (write);
    // get is the even CPU cycle of this power-on alignment. A pending DMC
    // fetch preempts an OAM get; its dummy/align cycles overlap the OAM
    // stream (wiki: DMC DMA during OAM DMA), costing one stolen get plus
    // one OAM realign. Every cycle advances PPU and APU catch-up.
    void dmaCycle()
    {
        sched->advanceMaster(12);
        ppu->runTo(sched->now());
        apu->runTo(sched->now());
    }

    bool onGetCycle() const { return (sched->now() / 12) % 2 == 0; }

    // The DMC get: consumes this get cycle and refills the sample buffer.
    void dmcGetCycle()
    {
        dmaCycle();
        apu->dmcDeliver(bus->read(apu->dmcFetchAddress()));
    }

    void oamDma(std::uint8_t page)
    {
        const std::uint16_t base = static_cast<std::uint16_t>(page << 8);
        dmaCycle();                          // halt cycle
        if (!onGetCycle()) {
            dmaCycle();                      // align to a get cycle
        }
        std::uint8_t pending = 0;
        bool haveByte = false;
        int i = 0;
        while (i < 256 || haveByte) {
            if (haveByte || !onGetCycle()) {
                // Put cycle: write the pending byte (or idle/realign when
                // a DMC get stole the preceding get cycle).
                dmaCycle();
                if (haveByte) {
                    ppu->oamDmaWrite(pending);
                    haveByte = false;
                }
                continue;
            }
            // Get cycle: the DMC takes priority over the OAM read. The
            // halt-parity gate is CPU-path only; the steal happens at the
            // next get cycle once the byte is wanted.
            if (apu->dmcGetWanted()) {
                dmcGetCycle();
                continue;
            }
            dmaCycle();
            pending = bus->read(static_cast<std::uint16_t>(base + i));
            haveByte = true;
            ++i;
        }
        // A fetch that became pending in the final pairs extends past the
        // DMA: its dummy/align overlapped the tail, leaving the get owed.
        if (apu->dmcGetWanted()) {
            dmcGetCycle();
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
    int mmc3RevSel = 0; // Machine::Mmc3Revision: 0 Auto, 1 B, 2 A
    std::unique_ptr<Ppu> ppu;
    std::unique_ptr<Apu> apu;
    std::unique_ptr<Cpu> cpu;

    IVideoSink* videoSink = nullptr;
    std::array<std::uint32_t, 256 * 240> framebuffer{};
    std::vector<std::uint8_t> stateBlob;

    std::uint16_t cartridgeId() const;
    SaveStateBlob capture();
    LoadStateError applyState(const std::uint8_t* data, std::size_t size);

    RunState runState = RunState::NeedsRom;
    std::uint64_t frameCounter = 0;
    ButtonSet controllers[2]{};
};

} // namespace cutenes
