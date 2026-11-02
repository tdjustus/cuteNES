#include <cutenes/qt/running_activity.hpp>

#include <windows.h>

// Windows: power throttling turned off while a game runs (see the header).
//
// Windows may run a background process, or one whose windows are minimized
// or hidden, at a lower execution speed (EcoQoS), and Windows 11 ignores
// timeBeginPeriod for a process whose windows are all hidden, so the frame
// clock's 1 ms sleeps become ~15.6 ms ones. A control mask with both bits
// and a zero state mask says "never throttle these"; a zero control mask
// hands the decision back to Windows, so a paused cuteNES is throttled
// like any idle app. Before Windows 10 1709 the call fails, and there is no
// throttling to turn off.

namespace cutenes::qt {

namespace {

void setThrottling(bool exempt)
{
    PROCESS_POWER_THROTTLING_STATE state{};
    state.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    state.ControlMask = exempt
        ? PROCESS_POWER_THROTTLING_EXECUTION_SPEED | PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION
        : 0;
    state.StateMask = 0;
    SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &state, sizeof state);
}

} // namespace

RunningActivity::~RunningActivity()
{
    hold(false);
}

void RunningActivity::hold(bool running)
{
    if (running == m_held) return;
    setThrottling(running);
    m_held = running;
}

} // namespace cutenes::qt
