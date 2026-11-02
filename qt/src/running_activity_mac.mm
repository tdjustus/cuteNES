#include <cutenes/qt/running_activity.hpp>

#import <Foundation/Foundation.h>

// macOS: an NSProcessInfo activity while a game runs (see the header).

namespace cutenes::qt {

RunningActivity::~RunningActivity()
{
    hold(false);
}

void RunningActivity::hold(bool running)
{
    if (running == m_held) return;
    if (running) {
        id<NSObject> activity = [[NSProcessInfo processInfo]
            beginActivityWithOptions:(NSActivityUserInitiatedAllowingIdleSystemSleep | NSActivityLatencyCritical)
                              reason:@"Running a game"];
        // Retained across ARC here; released by the __bridge_transfer below.
        m_token = (__bridge_retained void*)activity;
    } else if (m_token) {
        id<NSObject> activity = (__bridge_transfer id<NSObject>)m_token;
        [[NSProcessInfo processInfo] endActivity:activity];
        m_token = nullptr;
    }
    m_held = running;
}

} // namespace cutenes::qt
