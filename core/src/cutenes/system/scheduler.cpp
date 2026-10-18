#include "cutenes/system/scheduler.hpp"

namespace cutenes {

Scheduler::Scheduler(const RegionProfile& profile)
    : m_profile(&profile)
{
}

std::uint64_t Scheduler::masterTicksPerFrame() const
{
    return cutenes::masterTicksPerFrame(*m_profile);
}

void Scheduler::advanceMaster(std::uint64_t ticks)
{
    m_master += ticks;
}

std::uint64_t Scheduler::cpuCycleToMaster(std::uint64_t cpuCycles) const
{
    return cpuCycles * m_profile->cpuDivisor;
}

std::uint64_t Scheduler::endFrame()
{
    const std::uint64_t boundary = (m_frame + 1) * masterTicksPerFrame();
    if (m_master < boundary) {
        m_master = boundary;
    }
    ++m_frame;
    return m_master;
}

} // namespace cutenes
