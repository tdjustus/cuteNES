#include <cutenes/qt/running_activity.hpp>

// Everything but macOS: nothing to tell the OS (see the header).

namespace cutenes::qt {

RunningActivity::~RunningActivity() = default;

void RunningActivity::hold(bool running)
{
    m_held = running;
}

} // namespace cutenes::qt
