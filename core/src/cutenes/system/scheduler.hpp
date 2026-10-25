#pragma once

#include "cutenes/system/region.hpp"

#include <cstdint>

namespace cutenes {

// The sole time source (data-model.md integrity rule 2): a single monotonic
// master-clock counter. Components catch up to now() at bus-access
// boundaries (research.md D1); nothing else in the core owns a clock.
class Scheduler {
public:
    explicit Scheduler(const RegionProfile& profile);

    std::uint64_t now() const { return m_master; }
    std::uint64_t frameIndex() const { return m_frame; }
    std::uint64_t masterTicksPerFrame() const;

    void advanceMaster(std::uint64_t ticks);

    std::uint64_t cpuCycleToMaster(std::uint64_t cpuCycles) const;

    // Advance to the exact end of the current frame and begin the next.
    std::uint64_t endFrame();

    // Zero the counters (test/debug realignment only — never during
    // emulation; the master clock must stay monotonic).
    void reset();

    // Restore-only jump (save-state load, task T049).
    void forceTo(std::uint64_t master, std::uint64_t frame)
    {
        m_master = master;
        m_frame = frame;
    }

private:
    const RegionProfile* m_profile;
    std::uint64_t m_master = 0;
    std::uint64_t m_frame = 0;
};

// A component the CPU catches up to the current master clock at every bus
// access: the PPU (dot-accurate) and the APU (per CPU cycle) — research.md
// D1. Both stay nullptr in CPU unit tests.
class Tickable {
public:
    virtual ~Tickable() = default;
    virtual void runTo(std::uint64_t masterClock) = 0;
    // NMI line level (PPU only); the CPU edge-detects by sampling at phi2.
    virtual bool nmiLine() const { return false; }
    // True once (consumed) if a $2002 read landed in the suppression window
    // and this frame's NMI must be cancelled (06-suppression rows 5-6).
    virtual bool consumeNmiKill() { return false; }
    // CPU-serviced DMC memory fetch (APU only; task T037).
    virtual bool dmcFetchPending() const { return false; }
    // Same fetch without the CPU halt-parity gate: the OAM DMA engine
    // steals the next get cycle once the byte is wanted (wiki overlap).
    virtual bool dmcGetWanted() const { return false; }
    virtual std::uint16_t dmcFetchAddress() const { return 0; }
    virtual bool dmcHaltOnPutForAlign() const { return false; }
    virtual bool dmcIsReloadFetch() const { return false; }
    virtual void dmcDeliver(std::uint8_t) {}
};

} // namespace cutenes
