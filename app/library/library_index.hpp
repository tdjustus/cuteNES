#pragma once

// The library index (research R8, contracts/library-layout.md "Index
// cache"): every ROM under roms/ plus recently opened ones elsewhere, with
// header info and support status. It shows the cached list at once and
// refreshes it in the background; a file watcher on roms/ triggers rescans
// (FR-005).

#include "library/library.hpp"

#include <QDateTime>
#include <QFileSystemWatcher>
#include <QFutureWatcher>
#include <QHash>
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QVector>

#include <mutex>
#include <optional>

namespace cutenes::app {

class AppSettings;

struct LibraryEntry {
    QString path;         // absolute
    QString relativePath; // under roms/, or empty for a ROM elsewhere
    qint64 size = 0;
    qint64 mtime = 0;     // ms since the epoch
    QString identity;     // sha1; empty when the file is not an iNES image
    QString name;         // the file's stem
    int mapper = -1;
    int submapper = 0;
    QString region;       // NTSC, PAL or Unspecified
    bool battery = false;
    bool supported = false;
    QString reason;       // why the core rejects it

    // From the library and the settings (annotate()).
    QDateTime lastPlayed;
    bool hasSave = false;
    int stateCount = 0;
    bool recent = false;
};

// Reads one ROM: its identity, header and the core's verdict from a scratch
// load. Thread-safe.
LibraryEntry describeRom(const QString& path, const QString& romsDir = {});

// Every .nes file under `romsDir` (recursively) and every existing file in
// `extra`, reusing `previous` entries (by path) whose size and mtime are
// unchanged. Thread-safe.
QVector<LibraryEntry> scanRoms(const QString& romsDir, const QStringList& extra,
    const QHash<QString, LibraryEntry>& previous);

// Fills hasSave, stateCount, lastPlayed and recent.
void annotate(QVector<LibraryEntry>& entries, const Library& library, const AppSettings& settings);

bool writeIndexCache(const QString& file, const QString& root, const QVector<LibraryEntry>& entries);
QVector<LibraryEntry> readIndexCache(const QString& file, QString* root);

class LibraryIndex : public QObject {
    Q_OBJECT

public:
    // <CacheLocation>/library-index.json
    static QString defaultCacheFile();

    LibraryIndex(QString cacheFile, const AppSettings& settings, QObject* parent = nullptr);
    ~LibraryIndex() override;

    // Shows the cache for `library` (when it was for the same root), then
    // rescans in the background unless `scan` is false.
    void setLibrary(const Library& library, bool scan = true);
    // A copy of the current entries; safe from any thread.
    QVector<LibraryEntry> entries() const;
    bool scanning() const { return m_scan.isRunning(); }

public slots:
    // Rescans now, or once the running scan finishes.
    void rescan();
    // Re-reads only the saves, states, last played and recent flags.
    void refreshAnnotations();

signals:
    void entriesChanged();

private:
    void onScanFinished();
    void watchFolders();
    void publish(QVector<LibraryEntry> entries);

    QString m_cacheFile;
    const AppSettings& m_settings;
    std::optional<Library> m_library;
    mutable std::mutex m_mutex;
    QVector<LibraryEntry> m_entries;
    QFutureWatcher<QVector<LibraryEntry>> m_scan;
    bool m_rescanPending = false;
    QFileSystemWatcher m_watcher;
    QTimer m_debounce;
};

} // namespace cutenes::app
