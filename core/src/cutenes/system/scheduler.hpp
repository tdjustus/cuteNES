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

private:
    const RegionProfile* m_profile;
    std::uint64_t m_master = 0;
    std::uint64_t m_frame = 0;
};

} // namespace cutenes
