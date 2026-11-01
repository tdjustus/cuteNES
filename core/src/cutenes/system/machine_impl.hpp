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
#include <optional>

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
        // $4000-$4013, $4014 and $4018-$401F have no read drivers: they
        // return the CPU data-bus open-bus latch (blargg
        // cpu_exec_space_apu: write-only APU ports AND the unallocated
        // $4018-$40FF I/O space). $4020-$40FF is handled by the Bus
        // itself (also open bus).
        if (addr == 0x4015) {
            return apu->registerRead(addr);
        }
        if (addr != 0x4016 && addr != 0x4017) {
            return bus != nullptr ? bus->openBusRead() : 0;
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
    // 256 get/put pairs. CPU cycles alternate get (read) / put (write); the
    // first half of an APU cycle is a get, so CPU cycle 0 after reset is a
    // get (even cycles get, odd put). A pending DMC fetch preempts an OAM
    // get; its dummy/align cycles overlap the OAM stream (wiki: DMC DMA
    // during OAM DMA), costing one stolen get plus one OAM realign. Every
    // cycle advances PPU and APU catch-up.
    void dmaCycle()
    {
        sched->advanceMaster(12);
        ppu->runTo(sched->now());
        apu->runTo(sched->now());
    }

    // The cycle the next dmaCycle() would use, relative to the current
    // half-step position. "now" sits at the middle of the cycle in flight,
    // so the next cycle is now/12 + 1.
    bool nextIsGet() const
    {
        return ((sched->now() / 12) + 1) % 2 == 0;
    }

    // The DMC get: consumes this get cycle and refills the sample buffer.
    void dmcGetCycle()
    {
        dmaCycle();
        apu->dmcDeliver(bus->read(apu->dmcFetchAddress()));
    }

    void oamDma(std::uint8_t page)
    {
        const std::uint16_t base = static_cast<std::uint16_t>(page << 8);
        dmaCycle();                          // halt cycle (the cycle after $4014)
        if (!nextIsGet()) {
            dmaCycle();                      // align: first get must follow
        }
        std::uint8_t pending = 0;
        bool haveByte = false;
        int i = 0;
        // DMC overlap phase for the upcoming cycle (nesdev wiki DMA, "DMC DMA
        // during OAM DMA"): halt and dummy and the optional alignment are
        // no-op cycles that OVERLAP the OAM stream, while the DMC get steals
        // the OAM get and forces an OAM realign. 0 idle, 1 halt, 2 dummy,
        // 3 align, 4 the DMC get.
        int dmc = 0;
        while (i < 256 || haveByte || dmc != 0) {
            const bool isGet = nextIsGet();
            if (dmc == 0 && apu->dmcGetWanted()) {
                dmc = 1; // halt: overlaps this cycle's OAM access
            }
            if (dmc == 4) {
                // The DMC get owns this cycle; the skipped OAM get leaves
                // the following put with nothing to write (realign).
                dmaCycle();
                apu->dmcDeliver(bus->read(apu->dmcFetchAddress()));
                dmc = 0;
                continue;
            }
            // OAM work happens on halt/dummy/align cycles (no-op overlap).
            dmaCycle();
            if (isGet) {
                if (i < 256) {
                    pending = bus->read(static_cast<std::uint16_t>(base + i));
                    haveByte = true;
                    ++i;
                }
            } else if (haveByte) {
                ppu->oamDmaWrite(pending);
                haveByte = false;
            }
            if (dmc == 1) {
                dmc = 2; // dummy follows the halt
            } else if (dmc == 2) {
                // The cycle after the dummy: the DMC get if it is a get,
                // otherwise one alignment cycle first.
                dmc = isGet ? 3 : 4;
            } else if (dmc == 3) {
                dmc = 4;
            }
        }
        // The OAM stream is complete; nothing else to advance.
    }
};

// Private implementation detail of cutenes::Machine (pimpl). Components are
// constructed after the region resolves (load may select PAL), hence the
// unique_ptrs.
struct Machine::Impl {
    explicit Impl(const MachineConfig& config);

    void buildComponents(Region region);
    // The override, else the header (NTSC when unspecified); rebuilds the
    // timing-dependent components when that differs from the current one.
    void applyRegion();
    void deliverFrame();
    void runUntilFrameComplete();

    MachineConfig config;
    Region regionId = Region::Ntsc;
    std::optional<Region> regionOverride;
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
    IAudioSink* audioSink = nullptr; // reapplied when buildComponents() replaces the APU
    // Reinstalled when buildComponents() replaces the CPU and PPU; empty
    // means tracing is off.
    std::function<void(const TracePoint&)> trace;
    void installTrace();
    std::array<std::uint32_t, 256 * 240> framebuffer{}; // the default palette
    std::array<std::uint16_t, 256 * 240> indices{};     // Ppu::setIndexOutput
    // setPalette: the caller's entries, and the 512-entry table and frame
    // they make (64 entries get the region's emphasis when it is built).
    std::vector<std::uint32_t> palette;
    std::array<std::uint32_t, 512> paletteTable{};
    std::vector<std::uint32_t> paletteFrame;
    void buildPaletteTable();
    std::vector<std::uint8_t> stateBlob;

    std::uint16_t cartridgeId() const;
    SaveStateBlob capture();
    LoadStateError applyState(const std::uint8_t* data, std::size_t size);

    RunState runState = RunState::NeedsRom;
    std::uint64_t frameCounter = 0;
    ButtonSet controllers[2]{};
};

} // namespace cutenes
