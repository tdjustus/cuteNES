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

    // library.romFolders: folders the user added, scanned in place beside
    // the library's own roms/ (absolute paths, in the order added).
    QStringList romFolders() const;
    void setRomFolders(const QStringList& folders);

    // Recently opened ROMs, newest first, absolute paths.
    QStringList recent() const;
    void addRecent(const QString& path);
    void clearRecent();

    // When a game (by identity) was last opened; invalid if never.
    QDateTime lastPlayed(const QString& sha1) const;
    void setLastPlayed(const QString& sha1, const QDateTime& when);

    // The selected save-state slot of a game (1-10; 1 if never chosen).
    int selectedSlot(const QString& sha1) const;
    void setSelectedSlot(const QString& sha1, int slot);

    // servers.* (data-model "ServerSettings"): both off until the user
    // turns them on. A port of 0 picks a free one.
    struct ServerSettings {
        bool apiEnabled = false;
        int apiPort = 8765;
        bool mcpEnabled = false;
        int mcpPort = 8766;
    };
    ServerSettings servers() const;
    void setServers(const ServerSettings& servers);

    // video.showFps (off) and video.showOsd (on), data-model "Settings".
    bool showFps() const;
    void setShowFps(bool on);
    bool showOsd() const;
    void setShowOsd(bool on);

    // The last Settings page shown.
    QString lastSettingsPage() const;
    void setLastSettingsPage(const QString& page);

    void sync();

private:
    mutable std::mutex m_mutex;
    mutable QSettings m_settings;
};

} // namespace cutenes::app
