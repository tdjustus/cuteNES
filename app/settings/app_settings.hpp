#pragma once

// The application's settings (research R8, data-model "Settings"): an INI
// file in the per-user config location, separate from the library
// (FR-014). Safe to use from any thread: the GUI reads it, and gui.*
// commands change it on the emulation thread.

#include <QDateTime>
#include <QMap>
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

    // updates.* (data-model "Settings keys"; research R16): no request at
    // all until the user turns checks on (FR-090).
    struct UpdateSettings {
        bool check = false;
        bool asked = false;     // the first-launch question has been answered
        QString skippedVersion; // "1.2.0", or empty
    };
    UpdateSettings updates() const;
    void setUpdates(const UpdateSettings& updates);

    // video.* (data-model "Settings"): the window scale 1-6, the aspect
    // ("square", "8:7", "4:3"), each edge's crop 0-16, the filter
    // ("nearest", "smooth", "scanlines").
    struct VideoSettings {
        int scale = 3;
        bool fullscreen = false;
        bool integerScaling = true;
        QString aspect = QStringLiteral("8:7");
        int overscanTop = 8;
        int overscanBottom = 8;
        int overscanLeft = 0;
        int overscanRight = 0;
        QString filter = QStringLiteral("nearest");
        bool alwaysOnTop = false;
        QString pacing = QStringLiteral("auto"); // auto, display or audio (R4)
        QString palette = QStringLiteral("default"); // a built-in name, or a .pal file's path
        bool operator==(const VideoSettings&) const = default;
    };
    VideoSettings video() const;
    void setVideo(const VideoSettings& video);

    // audio.* (data-model "Settings"): the device by name ("default" for
    // the system's), the volume 0-100, mute, the latency 32-200 ms, and
    // fast-forward's sound ("stretch" up to 2x, or "mute").
    struct AudioSettings {
        QString device = QStringLiteral("default");
        int volume = 100;
        bool muted = false;
        int latencyMs = 64;
        QString fastForwardAudio = QStringLiteral("stretch");
    };
    AudioSettings audio() const;
    void setAudio(const AudioSettings& audio);

    // emulation.* (data-model "Settings"): pause when the window loses
    // focus, the automatic state on close (FR-037), and the fast-forward
    // speed in percent (150-400).
    struct EmulationSettings {
        bool pauseWhenInactive = true;
        bool resumeOnOpen = false;
        int fastForwardSpeed = 300;
        QString region = QStringLiteral("auto"); // auto, ntsc or pal (Emulation > Region)
    };
    EmulationSettings emulation() const;
    void setEmulation(const EmulationSettings& emulation);

    // video.showFps (off) and video.showOsd (on), data-model "Settings".
    bool showFps() const;
    void setShowFps(bool on);
    bool showOsd() const;
    void setShowOsd(bool on);

    // input.* (data-model "Settings"): the turbo rate in presses a second
    // (2-30), and the two input options.
    struct InputSettings {
        int turboRate = 15;
        bool allowOppositeDirections = false;
        bool pauseOnGamepadDisconnect = true;
    };
    InputSettings input() const;
    void setInput(const InputSettings& input);

    // The GUID of the pad that last drove player `port` (1 or 2), so it
    // gets that player back in a later run; empty if none.
    QString gamepad(int port) const;
    void setGamepad(int port, const QString& guid);

    // The key bindings that differ from the defaults, in the groups
    // `hotkeys` and `input` (contract "Key Bindings page"): each id's two
    // bindings, "" for none. An empty pair is a cleared entry.
    QMap<QString, QStringList> bindings(const QString& group) const;
    // Stores `pair`, or removes the id when it is empty (the default).
    void setBinding(const QString& group, const QString& id, const QStringList& pair);

    // The last Settings page shown.
    QString lastSettingsPage() const;
    void setLastSettingsPage(const QString& page);

    void sync();

private:
    mutable std::mutex m_mutex;
    mutable QSettings m_settings;
};

} // namespace cutenes::app
