#pragma once

// The decisions behind AudioOutput's dynamic rate control (research R4), as
// plain arithmetic on queue depths so they can be checked without a sound
// card (the pacing soak, quickstart A15, runs them for ten simulated
// minutes). AudioOutput feeds it the queue depth around each chunk it
// queues and applies what it returns to the SDL stream.
//
// The approach is general to any emulator whose sound is produced at the
// emulated machine's rate but played by a device with its own clock: keep
// a queue of about the target latency, start playing once it is full,
// steer its depth with a small, smoothed change of playback rate (inaudible
// at 0.5%), and drop a backlog that a stall left rather than play it late.

#include <cstdint>

namespace cutenes::qt {

class AudioPacer {
public:
    // The largest nudge to the playback rate.
    static constexpr double kMaxRateAdjust = 0.005;
    // The share of the way to the wanted rate taken per chunk: smoothing,
    // so the pitch never wobbles audibly.
    static constexpr double kSmoothing = 0.05;

    struct Action {
        bool start = false;       // resume the device: the queue is primed
        bool dropBacklog = false; // clear the queue, then queue this chunk again
        double rate = 1.0;        // the playback-rate ratio (before speed)
    };

    // A chunk was just queued: `before` and `after` samples were waiting
    // ahead of and including it; `target` is the target latency in samples.
    Action queued(int before, int after, int target);
    // The queue was cleared (pause, load, reset, seek): wait for a full
    // queue before playing again. The rate is kept, as the clocks are.
    void stop();
    // A new device: stopped, at the nominal rate.
    void reset();

    bool playing() const { return m_playing; }
    double rate() const { return m_rate; }
    // The queue was found empty while playing, counted as chunks arrive.
    std::uint64_t underruns() const { return m_underruns; }

private:
    bool m_playing = false;
    double m_rate = 1.0;
    std::uint64_t m_underruns = 0;
};

} // namespace cutenes::qt
