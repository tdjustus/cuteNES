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

// How frames are paced (R4). Sync to display runs the clock at the
// display's refresh rate (or a whole fraction of it) when that is within 1%
// of the console's, so every frame is shown exactly once; audio dynamic rate
// control absorbs the difference. Otherwise the clock keeps the console's
// rate (sync to audio) and each vsync shows the newest frame.
enum class PacingMode { Auto, Display, Audio };

struct Pacing {
    bool display = false;       // synced to the display
    int divisor = 1;            // a frame every Nth vsync
    double framesPerSecond = 0; // the clock's rate at 100% speed
};

// `displayHz` is 0 when the refresh rate is unknown. Display asks for sync
// to display but falls back to the console's rate when no multiple is close.
Pacing choosePacing(PacingMode mode, double displayHz, double consoleHz);

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
