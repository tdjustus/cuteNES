#pragma once

// The application's settings (research R8, data-model "Settings"): an INI
// file in the per-user config location, separate from the library
// (FR-014). Safe to use from any thread: the GUI reads it, and gui.*
// commands change it on the emulation thread.

#include <QDateTime>
#include <QSettings>
#include <QString>
#include <QStringList>

#include <mutex>

namespace cutenes::app {

class AppSettings {
public:
    static constexpr int kMaxRecent = 10;

    // <AppConfigLocation>/cutenes.ini
    static QString defaultFile();

    explicit AppSettings(const QString& file = defaultFile());

    QString fileName() const;

    // library.root: the saved location (Library::defaultRoot() when unset).
    QString libraryRoot() const;
    void setLibraryRoot(const QString& root);

    // Recently opened ROMs, newest first, absolute paths.
    QStringList recent() const;
    void addRecent(const QString& path);
    void clearRecent();

    // When a game (by identity) was last opened; invalid if never.
    QDateTime lastPlayed(const QString& sha1) const;
    void setLastPlayed(const QString& sha1, const QDateTime& when);

    // The last Settings page shown.
    QString lastSettingsPage() const;
    void setLastSettingsPage(const QString& page);

    void sync();

private:
    mutable std::mutex m_mutex;
    mutable QSettings m_settings;
};

} // namespace cutenes::app
