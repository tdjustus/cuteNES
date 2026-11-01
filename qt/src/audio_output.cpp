#include <cutenes/qt/audio_output.hpp>

#include <SDL3/SDL.h>

#include <algorithm>

namespace cutenes::qt {

AudioOutput::AudioOutput(int sampleRate) : m_sampleRate(sampleRate) {}

AudioOutput::~AudioOutput() { close(); }

bool AudioOutput::open()
{
    std::lock_guard lock(m_mutex);
    if (m_stream) {
        return true;
    }
    const SDL_AudioSpec spec{SDL_AUDIO_S16, 1, m_sampleRate};
    m_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (!m_stream) {
        m_error = SDL_GetError();
        return false;
    }
    // The device starts paused; onSamples starts it once a buffer is queued.
    m_error.clear();
    m_playing = false;
    m_rate = 1.0;
    return true;
}

void AudioOutput::close()
{
    std::lock_guard lock(m_mutex);
    if (m_stream) {
        SDL_DestroyAudioStream(m_stream);
        m_stream = nullptr;
    }
    m_playing = false;
}

bool AudioOutput::isOpen() const
{
    std::lock_guard lock(m_mutex);
    return m_stream != nullptr;
}

std::string AudioOutput::error() const
{
    std::lock_guard lock(m_mutex);
    return m_error;
}

int AudioOutput::queuedSamples() const
{
    const int bytes = SDL_GetAudioStreamQueued(m_stream);
    return bytes > 0 ? bytes / static_cast<int>(sizeof(std::int16_t)) : 0;
}

void AudioOutput::onSamples(const std::int16_t* pcm, std::size_t count, std::uint64_t)
{
    std::lock_guard lock(m_mutex);
    if (!m_stream || count == 0 || m_muted.load()) {
        return;
    }
    const int target = m_sampleRate * m_targetMs.load() / 1000;
    if (m_playing && queuedSamples() == 0) {
        ++m_underruns;
    }
    SDL_PutAudioStreamData(m_stream, pcm, static_cast<int>(count * sizeof(std::int16_t)));
    const int queued = queuedSamples();

    if (!m_playing) {
        if (queued >= target) {
            SDL_ResumeAudioStreamDevice(m_stream);
            m_playing = true;
        }
        return;
    }
    if (queued > 4 * target) {
        // Far behind (the machine was stalled): drop the backlog instead of
        // playing it seconds late.
        SDL_ClearAudioStream(m_stream);
        SDL_PutAudioStreamData(m_stream, pcm, static_cast<int>(count * sizeof(std::int16_t)));
    }
    // Proportional control on the queue depth, smoothed so the pitch never
    // wobbles audibly: above target, play slightly faster; below, slower.
    const double error = static_cast<double>(queued - target) / target;
    const double wanted = 1.0 + std::clamp(error * kMaxRateAdjust, -kMaxRateAdjust, kMaxRateAdjust);
    m_rate += (wanted - m_rate) * 0.05;
    SDL_SetAudioStreamFrequencyRatio(m_stream, static_cast<float>(m_rate * m_speed.load()));
}

void AudioOutput::setSpeed(double speed)
{
    m_speed.store(speed > 0 ? speed : 1.0);
}

void AudioOutput::setMuted(bool muted)
{
    if (m_muted.exchange(muted) == muted) return;
    if (muted) clear(); // nothing stale plays when it comes back
}

void AudioOutput::clear()
{
    std::lock_guard lock(m_mutex);
    if (!m_stream) {
        return;
    }
    SDL_PauseAudioStreamDevice(m_stream);
    SDL_ClearAudioStream(m_stream);
    m_playing = false;
}

void AudioOutput::setTargetLatencyMs(int ms)
{
    m_targetMs.store(std::clamp(ms, 32, 200));
}

AudioOutput::Stats AudioOutput::stats() const
{
    std::lock_guard lock(m_mutex);
    Stats s;
    s.rate = m_rate;
    s.underruns = m_underruns;
    if (m_stream) {
        s.queuedMs = queuedSamples() * 1000 / m_sampleRate;
    }
    return s;
}

} // namespace cutenes::qt
