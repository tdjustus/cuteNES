#include <cutenes/qt/audio_pacer.hpp>

#include <algorithm>

namespace cutenes::qt {

AudioPacer::Action AudioPacer::queued(int before, int after, int target)
{
    Action action;
    action.rate = m_rate;
    if (m_playing && before == 0) ++m_underruns;
    if (!m_playing) {
        if (after >= target) {
            action.start = true;
            m_playing = true;
        }
        return action;
    }
    // Far behind (the machine was stalled): drop the backlog instead of
    // playing it seconds late.
    action.dropBacklog = after > 4 * target;
    // Proportional control on the queue depth: above target, play slightly
    // faster; below, slower.
    const double error = static_cast<double>(after - target) / target;
    const double wanted = 1.0 + std::clamp(error * kMaxRateAdjust, -kMaxRateAdjust, kMaxRateAdjust);
    m_rate += (wanted - m_rate) * kSmoothing;
    action.rate = m_rate;
    return action;
}

void AudioPacer::stop()
{
    m_playing = false;
}

void AudioPacer::reset()
{
    m_playing = false;
    m_rate = 1.0;
}

} // namespace cutenes::qt
