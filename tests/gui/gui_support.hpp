#pragma once

// Helpers for the gui tests.

#include "mainwindow.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QTemporaryDir>
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

// Window options in a fresh folder of a process-wide temporary directory:
// settings, library and index cache never touch the user's own.
inline app::MainWindow::Options windowOptions(QString* folder = nullptr)
{
    static QTemporaryDir root;
    static int count = 0;
    const QString dir = QDir(root.path()).filePath(QString::number(++count));
    QDir().mkpath(dir);
    if (folder) *folder = dir;
    app::MainWindow::Options options;
    options.settingsFile = dir + QStringLiteral("/cutenes.ini");
    options.libraryRoot = dir + QStringLiteral("/library");
    options.indexCacheFile = dir + QStringLiteral("/library-index.json");
    options.batteryInterval = std::chrono::milliseconds(100);
    options.openExternally = false;
    return options;
}

} // namespace cutenes::test
