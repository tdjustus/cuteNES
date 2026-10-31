#pragma once

// Battery saves (spec FR-012, research R8, data-model "BatterySave"). The
// emulation thread calls open, poll and close with the machine; a writer
// thread puts changed RAM on disk at most once per interval, so a change is
// saved within one interval (5 s, under FR-012's 10 s). Every write is
// atomic (QSaveFile). A per-game QLockFile keeps a second instance from
// writing the same save.

#include "library/library.hpp"

#include <cutenes/machine.hpp>

#include <QByteArray>
#include <QLockFile>
#include <QString>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

namespace cutenes::app {

class BatterySaver {
public:
    explicit BatterySaver(std::chrono::milliseconds interval = std::chrono::seconds(5));
    ~BatterySaver();
    BatterySaver(const BatterySaver&) = delete;
    BatterySaver& operator=(const BatterySaver&) = delete;

    struct Opened {
        bool active = false; // this instance saves the game's battery RAM
        bool loaded = false; // a save was loaded into the machine
        QString path;        // the save file
        QString warning;     // for the user: a save set aside, a second instance
    };

    // Right after the cartridge is loaded and before it runs: finds the
    // game's save by identity and loads it. A save of the wrong size is
    // renamed <name>.bad-<YYYYMMDD-HHMMSS> and not loaded. A game without
    // battery RAM is inactive.
    Opened open(const Library& library, const GameIdentity& id, const QString& displayName,
        Machine& machine);
    // Hands changed battery RAM to the writer.
    void poll(const Machine& machine);
    // Writes any change now and releases the game.
    void close(const Machine& machine);

    bool active() const { return m_active; }
    QString path() const { return m_path; }
    int writes() const { return m_writes.load(); }

private:
    void run();
    void write(const QByteArray& bytes); // under m_writeMutex

    const std::chrono::milliseconds m_interval;
    bool m_active = false;
    QString m_path;
    std::unique_ptr<QLockFile> m_lock;
    QByteArray m_known; // the RAM as last handed over

    std::mutex m_mutex; // guards the pending write and the thread's state
    std::condition_variable m_wake;
    QByteArray m_pending;
    bool m_hasPending = false;
    bool m_stopping = false;
    std::chrono::steady_clock::time_point m_lastWrite{};
    std::mutex m_writeMutex;
    std::atomic<int> m_writes{0};
    std::thread m_thread;
};

} // namespace cutenes::app
