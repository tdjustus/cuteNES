#pragma once

// Requests frames at the console's rate from a timer thread (research R4,
// timer mode): 60.0988 per second for NTSC, 50.0070 for PAL. Audio dynamic
// rate control absorbs the difference between this clock and the sound
// card's.

#include <atomic>
#include <cstdint>
#include <functional>
#include <thread>

namespace cutenes::qt {

class FrameClock {
public:
    // `tick` runs on the clock's thread with the number of frames due
    // (normally 1); it must not block.
    explicit FrameClock(std::function<void(std::uint32_t)> tick);
    ~FrameClock();
    FrameClock(const FrameClock&) = delete;
    FrameClock& operator=(const FrameClock&) = delete;

    void start(double framesPerSecond);
    void stop();
    bool running() const { return m_running.load(); }
    // Takes effect from the next tick.
    void setRate(double framesPerSecond);
    double rate() const;

private:
    void run();

    std::function<void(std::uint32_t)> m_tick;
    std::atomic<bool> m_running{false};
    std::atomic<std::int64_t> m_periodNs{16'639'270}; // 1 / 60.0988 s
    std::thread m_thread;
};

} // namespace cutenes::qt
