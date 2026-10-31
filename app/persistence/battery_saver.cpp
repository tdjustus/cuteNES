#include "persistence/battery_saver.hpp"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

namespace cutenes::app {

namespace {

QByteArray sramBytes(const Machine& machine)
{
    const SramRange r = machine.sram();
    return QByteArray(reinterpret_cast<const char*>(r.data), static_cast<qsizetype>(r.size));
}

} // namespace

BatterySaver::BatterySaver(std::chrono::milliseconds interval)
    : m_interval(interval), m_thread([this] { run(); })
{
}

BatterySaver::~BatterySaver()
{
    {
        std::lock_guard lock(m_mutex);
        m_stopping = true;
    }
    m_wake.notify_all();
    m_thread.join();
}

BatterySaver::Opened BatterySaver::open(const Library& library, const GameIdentity& id,
    const QString& displayName, Machine& machine)
{
    Opened result;
    if (machine.sram().size == 0) return result;

    m_path = library.gameFile(Library::Folder::Saves, id, displayName, QStringLiteral(".sav"));
    result.path = m_path;
    m_lock = std::make_unique<QLockFile>(QFileInfo(m_path).path() + QLatin1Char('/')
        + QFileInfo(m_path).completeBaseName() + QStringLiteral(".lock"));
    m_lock->setStaleLockTime(0); // held for as long as the game is open
    if (!m_lock->tryLock(0)) {
        m_lock.reset();
        result.warning = QObject::tr("This game is open in another cuteNES window, so its battery "
                                     "save will not be written from this one.");
        return result;
    }

    if (QFile file(m_path); file.exists()) {
        const QByteArray saved = file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
        if (saved.size() == static_cast<qsizetype>(machine.sram().size)) {
            machine.loadSram(reinterpret_cast<const std::uint8_t*>(saved.constData()),
                static_cast<std::size_t>(saved.size()));
            result.loaded = true;
        } else {
            // Never loaded, never deleted: kept for the user to inspect.
            const QString aside = m_path + QStringLiteral(".bad-")
                + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
            file.close();
            QFile::rename(m_path, aside);
            result.warning = QObject::tr("The battery save %1 has the wrong size and was set aside as %2.")
                                 .arg(QFileInfo(m_path).fileName(), QFileInfo(aside).fileName());
        }
    }
    library.recordIdentity(Library::Folder::Saves, m_path, id);
    m_known = sramBytes(machine);
    m_active = true;
    result.active = true;
    return result;
}

void BatterySaver::poll(const Machine& machine)
{
    if (!m_active) return;
    QByteArray now = sramBytes(machine);
    if (now == m_known) return;
    m_known = now;
    {
        std::lock_guard lock(m_mutex);
        m_pending = std::move(now);
        m_hasPending = true;
    }
    m_wake.notify_all();
}

void BatterySaver::close(const Machine& machine)
{
    if (!m_active) return;
    poll(machine);
    QByteArray last;
    bool has = false;
    {
        std::lock_guard lock(m_mutex);
        has = m_hasPending;
        last = std::move(m_pending);
        m_hasPending = false;
    }
    if (has) {
        std::lock_guard lock(m_writeMutex);
        write(last);
    } else if (!QFile::exists(m_path)) {
        // A game closed before it ever changed its RAM still gets its file.
        std::lock_guard lock(m_writeMutex);
        write(m_known);
    }
    m_lock.reset();
    m_active = false;
}

void BatterySaver::run()
{
    std::unique_lock lock(m_mutex);
    while (true) {
        m_wake.wait(lock, [this] { return m_stopping || m_hasPending; });
        if (m_stopping) return;
        // At most one write per interval; newer RAM replaces older meanwhile.
        const auto due = m_lastWrite + m_interval;
        if (std::chrono::steady_clock::now() < due) {
            m_wake.wait_until(lock, due, [this] { return m_stopping; });
            if (m_stopping) return;
            if (!m_hasPending) continue; // close() took it
        }
        QByteArray bytes = std::move(m_pending);
        m_hasPending = false;
        m_lastWrite = std::chrono::steady_clock::now();
        lock.unlock();
        {
            std::lock_guard writing(m_writeMutex);
            write(bytes);
        }
        lock.lock();
    }
}

void BatterySaver::write(const QByteArray& bytes)
{
    QSaveFile file(m_path);
    if (file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit()) {
        ++m_writes;
    }
}

} // namespace cutenes::app
