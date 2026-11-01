#pragma once

// The game's sound on the default output device, through an SDL3 audio
// stream (research R4). Dynamic rate control keeps the queue near its
// target latency by nudging the playback rate by at most ±0.5%, so the gap
// between the emulator's clock and the sound card's never becomes a gap or
// a growing delay.

#include <cutenes/media.hpp>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>

struct SDL_AudioStream;

namespace cutenes::qt {

class AudioOutput final : public IAudioSink {
public:
    // The largest nudge to the playback rate (R4).
    static constexpr double kMaxRateAdjust = 0.005;

    explicit AudioOutput(int sampleRate = 48000);
    ~AudioOutput();
    AudioOutput(const AudioOutput&) = delete;
    AudioOutput& operator=(const AudioOutput&) = delete;

    // Opens the default playback device. False (with error()) leaves the
    // output silent; samples are then dropped (spec US7-5).
    bool open();
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

    const int m_sampleRate;
    mutable std::mutex m_mutex;
    SDL_AudioStream* m_stream = nullptr;
    std::string m_error;
    bool m_playing = false; // the device is consuming (primed)
    double m_rate = 1.0;
    std::uint64_t m_underruns = 0;
    std::atomic<int> m_targetMs{64};
    std::atomic<double> m_speed{1.0};
    std::atomic<bool> m_muted{false};
};

} // namespace cutenes::qt
