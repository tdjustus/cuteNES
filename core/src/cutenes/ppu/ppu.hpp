#pragma once

#include "cutenes/bus/bus.hpp"
#include "cutenes/system/scheduler.hpp"

#include <cutenes/media.hpp>

#include <array>
#include <cstdint>
#include <functional>

namespace cutenes {

class StateReader;
class StateWriter;

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
    // The decayed open-bus latch (0 once the last PPU-port access is stale).
    std::uint8_t decayedBusValue() const;
    // Charge the latch bits selected by `mask` with `value` (a no-op for
    // undriven lines).
    void chargeBusBits(std::uint8_t mask, std::uint8_t value);
    void cpuWrite(std::uint16_t addr, std::uint8_t value) override;

    // Advance dots to the given master clock.
    void runTo(std::uint64_t masterClock) override;

    // Cold boot vs reset button (register state, write-ignore window).
    void reset(bool cold);

    // Save-state section "PPU " (task T049).
    void saveState(StateWriter& w) const;
    void loadState(StateReader& r);

    // Side-effect-free debug access (002 FR-015): no latch updates, no
    // v increments, no status clears. Register views are reconstructed:
    // $2004 = OAM[oamAddr], $2005 = fineX, $2006 = current v,
    // $2007 = the read buffer.
    void debugReadRegisters(std::uint8_t dst[8]) const;
    void debugReadVram(std::uint16_t addr, std::size_t len,
                       std::uint8_t* dst) const;
    void debugWriteVram(std::uint16_t addr, const std::uint8_t* src,
                        std::size_t len);
    void debugReadOam(std::uint8_t dst[256]) const;
    void debugWriteOam(const std::uint8_t src[256]);
    void debugReadPalette(std::uint8_t dst[32]) const;
    void debugWritePalette(const std::uint8_t src[32]);

    // OAM DMA target: 256 bytes pushed by the $4014 engine.
    void oamDmaWrite(std::uint8_t value);
    void clockIfA12Rose(); // $2007 access: v++ and A12-rise IRQ
    void trackA12(bool high); // shared guarded A12 edge

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
    std::uint8_t ppuBusRead(std::uint16_t addr) const; // $0000-$3FFF
    void ppuBusWrite(std::uint16_t addr, std::uint8_t value);
    std::uint8_t paletteRead(std::uint8_t index) const;

    const RegionProfile& m_profile;
    Scheduler& m_sched;
    Cartridge* m_cart = nullptr;
    std::uint32_t* m_frame = nullptr; // consumer-owned 256x240 buffer

    // Register file + scroll latches.
    std::uint8_t m_ctrl = 0, m_mask = 0, m_status = 0;
    // $2002 bit 7 as of the previous dot: the read path observes the
    // VBL flag one dot behind the live level the NMI sees (03-vbl_clear_time
    // vs 07-nmi_on_timing; timing.hpp item 3/4).
    std::uint8_t m_statusLag = 0;
    std::uint16_t m_v = 0, m_t = 0;
    std::uint8_t m_fineX = 0, m_w = 0, m_readBuffer = 0;
    std::uint64_t m_lastVramReadMaster = ~0ull; // double-2007 quirk state
    std::uint8_t m_lastVramReadValue = 0;
    std::uint8_t m_decay = 0; // open-bus latch for $2002 low bits
    // RC decay of the open-bus latch (blargg ppu_open_bus): each bit is a
    // capacitor charged only when its register drives that bus line,
    // bleeding to zero in well under a second (#3). $2002 drives ONLY
    // bits 7-5 (the low 5 keep decaying, #7), a palette $2007 read drives
    // only bits 5-0 (#8), and reads of write-only registers charge
    // nothing (#5). Per-bit charge timestamps, one ~0.5 s time constant.
    std::uint64_t m_decayMaster[8] = {};
    std::uint64_t m_decayTicks = 0;
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
    bool m_evalDone = false; // n wrapped past sprite 63 this line
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
    bool mA12High = false;        // PPU A12 line state (pattern bus)
    std::uint64_t mA12LowStart = 0;
    std::uint64_t m_ignoreWritesUntil = 0; // master clock

    static const std::array<std::uint32_t, 64> kPalette;
};

} // namespace cutenes
