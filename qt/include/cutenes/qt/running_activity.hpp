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
// General, not NES-specific: any emulator or real-time app paced by timers
// needs this on macOS. On Windows and Linux this only records the state for
// now. Their equivalents are not handled yet (#151): Windows 11's power
// throttling of background processes, which also ignores timeBeginPeriod for
// hidden windows, and Wayland withholding frame callbacks from hidden
// windows.

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
