#pragma once

// Helpers for the gui tests.

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>

#include <functional>

namespace cutenes::test {

// Processes events until `done` holds, for at most `ms`.
inline bool waitUntil(const std::function<bool()>& done, int ms = 5000)
{
    QElapsedTimer timer;
    timer.start();
    while (!done()) {
        if (timer.elapsed() > ms) return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(2);
    }
    return true;
}

} // namespace cutenes::test
