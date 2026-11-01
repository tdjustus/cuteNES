#pragma once

// The game's sound on the default output device, through an SDL3 audio
// stream (research R4). Dynamic rate control keeps the queue near its
// target latency by nudging the playback rate by at most ±0.5%, so the gap
// between the emulator's clock and the sound card's never becomes a gap or
// a growing delay.

#include <cutenes/media.hpp>
#include <cutenes/qt/audio_pacer.hpp>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

struct SDL_AudioStream;

namespace cutenes::qt {

class AudioOutput final : public IAudioSink {
public:
    // The largest nudge to the playback rate (R4).
    static constexpr double kMaxRateAdjust = AudioPacer::kMaxRateAdjust;

    explicit AudioOutput(int sampleRate = 48000);
    ~AudioOutput();
    AudioOutput(const AudioOutput&) = delete;
    AudioOutput& operator=(const AudioOutput&) = delete;

    // The playback devices' names.
    static std::vector<std::string> devices();

    // Opens `device` by name, or the default playback device (which SDL
    // moves to the system's new default by itself). A named device that is
    // not there opens the default instead, and device() is then empty.
    // False (with error()) leaves the output silent; samples are then
    // dropped (spec US7-5).
    bool open(const std::string& device = {});
    std::string device() const; // empty: the default
    void close();
    bool isOpen() const;
    std::string error() const;

    // The emulation thread's samples (16-bit mono).
    void onSamples(const std::int16_t* pcm, std::size_t count,
        std::uint64_t startMasterCycle) override;

    // Drops what is queued and waits for a full buffer before playing again:
    // on pause, load, reset and seek, so stale sound never plays.
    void clear();

    // The game's speed (1 = the console's): sound plays that much faster or
    // slower, pitch included (spec FR-035).
    void setSpeed(double speed);
    double speed() const { return m_speed.load(); }
    // Muted output drops what it is given (fast-forward above 2x, rewind).
    void setMuted(bool muted);
    bool muted() const { return m_muted.load(); }

    // The player's volume (0-100) and mute (audio.volume, audio.muted).
    void setVolume(int percent);
    int volume() const { return m_volume.load(); }
    void setUserMuted(bool muted);
    bool userMuted() const { return m_userMuted.load(); }

    // Reads SDL's audio device events (GUI thread). When the open device
    // goes away, sound moves to the default device; when no device is left,
    // the output is closed and silent. Returns what happened.
    enum class DeviceChange { None, MovedToDefault, Lost };
    DeviceChange poll();

    void setTargetLatencyMs(int ms); // default 64 (data-model "audio.latencyMs")
    int targetLatencyMs() const { return m_targetMs.load(); }

    struct Stats {
        int queuedMs = 0;
        double rate = 1.0;           // the current playback-rate ratio
        std::uint64_t underruns = 0; // the queue ran dry while playing
    };
    Stats stats() const;

private:
    int queuedSamples() const; // m_mutex held
    bool openLocked(const std::string& device); // m_mutex held
    void applyGain();                           // m_mutex held

    const int m_sampleRate;
    mutable std::mutex m_mutex;
    SDL_AudioStream* m_stream = nullptr;
    std::string m_error;
    std::string m_device; // the open device's name; empty: the default
    AudioPacer m_pacer; // whether the device plays, and at what rate
    std::atomic<int> m_targetMs{64};
    std::atomic<double> m_speed{1.0};
    std::atomic<bool> m_muted{false};
    std::atomic<int> m_volume{100};
    std::atomic<bool> m_userMuted{false};
};

} // namespace cutenes::qt
