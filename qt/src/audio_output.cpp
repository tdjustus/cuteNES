#include <cutenes/qt/audio_output.hpp>

#include <SDL3/SDL.h>

#include <algorithm>

namespace cutenes::qt {

AudioOutput::AudioOutput(int sampleRate) : m_sampleRate(sampleRate) {}

AudioOutput::~AudioOutput() { close(); }

std::vector<std::string> AudioOutput::devices()
{
    std::vector<std::string> names;
    if (!SDL_WasInit(SDL_INIT_AUDIO)) return names;
    int count = 0;
    SDL_AudioDeviceID* ids = SDL_GetAudioPlaybackDevices(&count);
    for (int i = 0; ids && i < count; ++i) {
        if (const char* name = SDL_GetAudioDeviceName(ids[i])) names.emplace_back(name);
    }
    SDL_free(ids);
    return names;
}

bool AudioOutput::open(const std::string& device)
{
    std::lock_guard lock(m_mutex);
    if (m_stream && device == m_device) {
        return true;
    }
    return openLocked(device);
}

bool AudioOutput::openLocked(const std::string& device)
{
    if (m_stream) {
        SDL_DestroyAudioStream(m_stream);
        m_stream = nullptr;
    }
    SDL_AudioDeviceID id = SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK;
    m_device.clear();
    if (!device.empty() && SDL_WasInit(SDL_INIT_AUDIO)) {
        int count = 0;
        SDL_AudioDeviceID* ids = SDL_GetAudioPlaybackDevices(&count);
        for (int i = 0; ids && i < count; ++i) {
            const char* name = SDL_GetAudioDeviceName(ids[i]);
            if (name && device == name) {
                id = ids[i];
                m_device = device;
            }
        }
        SDL_free(ids);
    }
    const SDL_AudioSpec spec{SDL_AUDIO_S16, 1, m_sampleRate};
    m_stream = SDL_OpenAudioDeviceStream(id, &spec, nullptr, nullptr);
    if (!m_stream) {
        m_error = SDL_GetError();
        m_device.clear();
        return false;
    }
    // The device starts paused; onSamples starts it once a buffer is queued.
    m_error.clear();
    m_pacer.reset();
    applyGain();
    return true;
}

std::string AudioOutput::device() const
{
    std::lock_guard lock(m_mutex);
    return m_device;
}

void AudioOutput::applyGain()
{
    if (m_stream) SDL_SetAudioStreamGain(m_stream, m_userMuted.load() ? 0.0f : static_cast<float>(m_volume.load()) / 100.0f);
}

void AudioOutput::setVolume(int percent)
{
    m_volume.store(std::clamp(percent, 0, 100));
    std::lock_guard lock(m_mutex);
    applyGain();
}

void AudioOutput::setUserMuted(bool muted)
{
    m_userMuted.store(muted);
    std::lock_guard lock(m_mutex);
    applyGain();
}

AudioOutput::DeviceChange AudioOutput::poll()
{
    DeviceChange change = DeviceChange::None;
    if (!SDL_WasInit(SDL_INIT_AUDIO)) return change;
    SDL_Event e;
    while (SDL_PeepEvents(&e, 1, SDL_GETEVENT, SDL_EVENT_AUDIO_DEVICE_ADDED, SDL_EVENT_AUDIO_DEVICE_FORMAT_CHANGED) > 0) {
        if (e.type != SDL_EVENT_AUDIO_DEVICE_REMOVED || e.adevice.recording) continue;
        std::lock_guard lock(m_mutex);
        if (!m_stream || SDL_GetAudioStreamDevice(m_stream) != e.adevice.which) continue;
        // Our device went: the default, or silence (US7-5).
        change = openLocked({}) ? DeviceChange::MovedToDefault : DeviceChange::Lost;
        if (change == DeviceChange::Lost && m_stream) {
            SDL_DestroyAudioStream(m_stream);
            m_stream = nullptr;
        }
    }
    return change;
}

void AudioOutput::close()
{
    std::lock_guard lock(m_mutex);
    if (m_stream) {
        SDL_DestroyAudioStream(m_stream);
        m_stream = nullptr;
    }
    m_pacer.stop();
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
    const int before = queuedSamples();
    SDL_PutAudioStreamData(m_stream, pcm, static_cast<int>(count * sizeof(std::int16_t)));
    const bool wasPlaying = m_pacer.playing();
    const AudioPacer::Action action = m_pacer.queued(before, queuedSamples(), target);
    if (action.start) SDL_ResumeAudioStreamDevice(m_stream);
    if (!wasPlaying) return;
    if (action.dropBacklog) {
        SDL_ClearAudioStream(m_stream);
        SDL_PutAudioStreamData(m_stream, pcm, static_cast<int>(count * sizeof(std::int16_t)));
    }
    SDL_SetAudioStreamFrequencyRatio(m_stream, static_cast<float>(action.rate * m_speed.load()));
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
    m_pacer.stop();
}

void AudioOutput::setTargetLatencyMs(int ms)
{
    m_targetMs.store(std::clamp(ms, 32, 200));
}

AudioOutput::Stats AudioOutput::stats() const
{
    std::lock_guard lock(m_mutex);
    Stats s;
    s.rate = m_pacer.rate();
    s.underruns = m_pacer.underruns();
    if (m_stream) {
        s.queuedMs = queuedSamples() * 1000 / m_sampleRate;
    }
    return s;
}

} // namespace cutenes::qt
