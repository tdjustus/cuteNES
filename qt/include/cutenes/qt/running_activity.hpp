#pragma once

// Tells the OS that a running game is time-critical work the user started,
// so the OS does not throttle it while its window is hidden (#124).
//
// macOS's App Nap slows the timers of an app whose windows are not visible:
// with the display asleep, a game driven through the MCP ran at a few frames
// a second. NSProcessInfo's activity API opts out for as long as an activity
// is held. RunningActivity holds one, "user-initiated" (no App Nap) and
// "latency-critical" (no timer coalescing), while the window says a game is
// running, and ends it on pause or close, so an idle cuteNES still naps and
// the Mac may still sleep when idle.
//
// Windows (#151) throttles background and hidden-window processes (EcoQoS)
// and, on Windows 11, ignores timeBeginPeriod for a process whose windows
// are all hidden. While a game runs, RunningActivity opts out of both with
// SetProcessInformation(ProcessPowerThrottling), and on pause hands the
// decision back to Windows.
//
// Linux needs nothing: no desktop throttles a hidden window's timers the
// way App Nap does. Wayland withholds frame callbacks from hidden windows,
// but the frame clock never waits on them: display pacing only sets the
// clock's rate to the display's (MainWindow::playbackChanged).
//
// General, not NES-specific: any emulator or real-time app paced by timers
// needs the macOS and Windows opt-outs.

namespace cutenes::qt {

class RunningActivity {
public:
    RunningActivity() = default;
    ~RunningActivity();
    RunningActivity(const RunningActivity&) = delete;
    RunningActivity& operator=(const RunningActivity&) = delete;

    // Begins the activity (true) or ends it (false); repeating either is a no-op.
    void hold(bool running);
    bool held() const { return m_held; }

private:
    bool m_held = false;
    void* m_token = nullptr; // macOS: the retained id<NSObject> from beginActivityWithOptions
};

} // namespace cutenes::qt
