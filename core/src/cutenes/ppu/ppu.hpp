#pragma once

#include "cutenes/bus/bus.hpp"
#include "cutenes/system/scheduler.hpp"

#include <cutenes/media.hpp>

#include <array>
#include <cstdint>
#include <functional>

namespace cutenes {

struct Cartridge;

// The 2C02 PPU: dot-stepped against the master clock (one dot per 4 master
// ticks NTSC / 5 PAL). Rendering pipeline, sprite evaluation (US1 correct
// selection; the per-dot overflow-bug state machine lands with task T035),
// VBlank/NMI at exact dots, register file $2000-$2007 with the documented
// latch behavior and post-reset write-ignore window (research.md D2/D3/D4).
class Ppu final : public CpuBusDevice, public Tickable {
public:
    Ppu(const RegionProfile& profile, Scheduler& scheduler);

    void attachCartridge(Cartridge* cartridge);
    void setVideoOutput(std::uint32_t* framebuffer);

    // NMI line: vblank flag AND enable (the CPU samples and edge-detects).
    bool consumeNmiKill() override
    {
        if (m_nmiKilled) {
            m_nmiKilled = false;
            return true;
        }
        return false;
    }

    bool nmiLine() const override
    {
        const bool high =
            (m_status & 0x80) != 0 && (m_ctrl & 0x80) != 0;
        return high;
    }


    // CpuBusDevice ($2000-$2007, pre-mirrored by the bus).
    std::uint8_t cpuRead(std::uint16_t addr) override;
    void cpuWrite(std::uint16_t addr, std::uint8_t value) override;

    // Advance dots to the given master clock.
    void runTo(std::uint64_t masterClock) override;

    // Cold boot vs reset button (register state, write-ignore window).
    void reset(bool cold);

    // OAM DMA target: 256 bytes pushed by the $4014 engine.
    void oamDmaWrite(std::uint8_t value);

    // Frame completion flag: set once the final visible pixel is emitted
    // (scanline 239, dot 255); the machine consumes and clears it.
    bool takeFrameReady();

    // Timing introspection (nestest log "PPU: column,scanline" needs it).
    std::uint32_t scanline() const { return static_cast<std::uint32_t>(m_scanline); }
    std::uint32_t dot() const { return static_cast<std::uint32_t>(m_dot); }
    std::uint64_t dotsExecuted() const { return m_dots; }

    // Rendering enabled (background or sprites) at this instant.
    bool renderingEnabled() const;

    // Register-file view for save states (task T048/T049).
    std::uint8_t ctrl() const { return m_ctrl; }

private:
    void stepDot();
    void fetchCycle(std::uint32_t dot);
    void incrementX();
    void incrementY();
    void copyHorizontal();
    void copyVertical();
    void renderPixel(std::uint32_t x);
    void evalDot(std::uint64_t dot); // per-dot evaluation (dots 65-256)
    void loadSpriteUnits();   // dots 257-320: fetch patterns
    void evaluateSpritesForLine0(); // pre-render: no evaluation (quirk)
    void setBackgroundShifters();

    std::uint16_t nametableIndex(std::uint16_t addr) const;
    std::uint8_t ppuBusRead(std::uint16_t addr); // $0000-$3FFF incl. palette
    void ppuBusWrite(std::uint16_t addr, std::uint8_t value);
    std::uint8_t paletteRead(std::uint8_t index) const;

    const RegionProfile& m_profile;
    Scheduler& m_sched;
    Cartridge* m_cart = nullptr;
    std::uint32_t* m_frame = nullptr; // consumer-owned 256x240 buffer

    // Register file + scroll latches.
    std::uint8_t m_ctrl = 0, m_mask = 0, m_status = 0;
    std::uint16_t m_v = 0, m_t = 0;
    std::uint8_t m_fineX = 0, m_w = 0, m_readBuffer = 0;
    std::uint8_t m_decay = 0; // open-bus latch for $2002 low bits
    std::uint8_t m_oamAddr = 0;

    // Memories.
    std::array<std::uint8_t, 2048> m_vram{};
    std::array<std::uint8_t, 32> m_palette{};
    std::array<std::uint8_t, 256> m_oam{};

    // Background pipeline.
    std::uint16_t m_shiftLo = 0, m_shiftHi = 0;
    std::uint16_t m_shiftAttrLo = 0, m_shiftAttrHi = 0;
    std::uint8_t m_latchNT = 0, m_latchAT = 0, m_latchLo = 0, m_latchHi = 0;

    // Sprites.
    struct Unit {
        std::uint8_t x = 0xFF, attr = 0, lo = 0, hi = 0;
    };
    std::array<Unit, 8> m_units{};
    int m_unitCount = 0;
    bool m_spriteZeroNext = false, m_spriteZeroActive = false;
    std::array<std::uint8_t, 32> m_oam2{};

    // Timing.
    std::uint64_t m_dots = 0;
    int m_scanline = 0; // 0-239 visible, 241-260 vblank, 261 pre-render
    int m_dot = 0;
    bool m_oddFrame = false;
    bool m_suppressSet = false; // read 1 dot before the set
    bool m_nmiKilled = false;   // $2002 read inside the NMI kill window

    // Per-dot sprite evaluation state (research.md D3; task T035). OAMADDR
    // itself is the evaluation pointer: evaluation starts at its dot-65
    // value and increments it as it scans.
    int m_evalStep = 0;         // copy substep 0-3
    int m_secCount = 0;         // sprites found (0-8)
    int m_secByte = 0;          // secondary OAM write index (0-31)
    int m_ovM = 0;              // overflow-scan byte offset (the m of the bug)
    bool m_evalOverflow = false;
    bool m_secSecondaryRead = false;
    std::uint8_t m_evalRead = 0; // odd-cycle OAM read buffer
    bool m_frameComplete = false; // final visible pixel emitted
    std::uint64_t m_ignoreWritesUntil = 0; // master clock

    static const std::array<std::uint32_t, 64> kPalette;
};

} // namespace cutenes
