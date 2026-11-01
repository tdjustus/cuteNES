#include <cutenes/qt/frame_clock.hpp>

#include <chrono>
#include <cmath>

#if defined(_WIN32)
// Windows sleeps in ~15.6 ms steps unless the timer resolution is raised.
#include <windows.h>
#include <timeapi.h>
#endif

namespace cutenes::qt {

namespace {

std::int64_t periodFor(double framesPerSecond)
{
    return static_cast<std::int64_t>(1e9 / framesPerSecond);
}

} // namespace

Pacing choosePacing(PacingMode mode, double displayHz, double consoleHz)
{
    if (mode != PacingMode::Audio && displayHz > 0) {
        constexpr int kMaxDivisor = 4;
        for (int n = 1; n <= kMaxDivisor; ++n) {
            const double rate = displayHz / n;
            if (std::abs(rate - consoleHz) <= consoleHz * 0.01) return {true, n, rate};
        }
    }
    return {false, 1, consoleHz};
}

FrameClock::FrameClock(std::function<void(std::uint32_t)> tick) : m_tick(std::move(tick)) {}

FrameClock::~FrameClock() { stop(); }

void FrameClock::start(double framesPerSecond)
{
    stop();
    setRate(framesPerSecond);
    m_running = true;
    m_thread = std::thread([this] { run(); });
}

void FrameClock::stop()
{
    m_running = false;
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

void FrameClock::setRate(double framesPerSecond)
{
    if (framesPerSecond > 0) {
        m_periodNs = periodFor(framesPerSecond);
    }
}

double FrameClock::rate() const
{
    return 1e9 / static_cast<double>(m_periodNs.load());
}

void FrameClock::run()
{
#if defined(_WIN32)
    timeBeginPeriod(1);
#endif
    using clock = std::chrono::steady_clock;
    auto next = clock::now();
    while (m_running.load()) {
        const auto period = std::chrono::nanoseconds(m_periodNs.load());
        next += period;
        std::this_thread::sleep_until(next);
        const auto now = clock::now();
        // After a long stall (a suspended laptop, a debugger), start afresh
        // instead of firing a burst of ticks to catch up.
        if (now - next > 4 * period) {
            next = now;
        }
        m_tick(1);
    }
#if defined(_WIN32)
    timeEndPeriod(1);
#endif
}

} // namespace cutenes::qt
