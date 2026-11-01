#pragma once

// Settings (contracts/menus-and-commands.md "Settings"): one page per
// group. G2 fills the Library page, G3 the Servers page, G4 the Input page
// and G5 the Video, Audio and Emulation pages; the others arrive with their phases. Hotkeys are on the Key
// Bindings page, which Input links to.

#include <QDialog>

class QCheckBox;
class QComboBox;
class QSlider;
class QLabel;
class QListWidget;
class QSpinBox;
class QPushButton;
class QStackedWidget;

namespace cutenes::app {

class SettingsDialog final : public QDialog {
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget* parent = nullptr);

    // "video", "audio", "input", "emulation", "library" or "servers".
    void showPage(const QString& page);
    QString currentPage() const;
    void setLibraryRoot(const QString& root);
    // The folders the library scans: its own roms folder (always first, not
    // removable) and the added ones.
    void setRomFolders(const QString& roms, const QStringList& added);

    // Settings > Servers: what is saved, and what is running.
    struct Server {
        bool enabled = false;
        int port = 0;
        bool running = false;
        QString url;
        QString token;
        QString tokenFile;
    };
    void setServers(const Server& api, const Server& mcp);

    // Settings > Input: the saved options, and the pads plugged in ("Player
    // 1: name").
    struct Input {
        int turboRate = 15;
        bool allowOppositeDirections = false;
        bool pauseOnGamepadDisconnect = true;
        QStringList gamepads;
    };
    void setInput(const Input& input);

    // Settings > Video (data-model "Settings" video.*).
    struct Video {
        int scale = 3;
        bool fullscreen = false;
        bool integerScaling = true;
        QString aspect;
        int overscan[4] = {8, 8, 0, 0}; // top, bottom, left, right
        QString filter;
        bool alwaysOnTop = false;
        bool showFps = false;
        bool showOsd = true;
        QString pacing;
        QString pacingNow; // what the pacing is doing, in words
    };
    void setVideo(const Video& video);

    // Settings > Audio and Emulation (audio.*, emulation.*).
    struct Audio {
        QString device = QStringLiteral("default");
        QStringList devices; // the playback devices now
        int volume = 100;
        bool muted = false;
        int latencyMs = 64;
        QString fastForwardAudio = QStringLiteral("stretch");
    };
    void setAudio(const Audio& audio);
    struct Emulation {
        bool pauseWhenInactive = true;
        bool resumeOnOpen = false;
        int fastForwardSpeed = 300;
    };
    void setEmulation(const Emulation& emulation);

signals:
    void pageShown(const QString& page);
    void changeLibraryRequested();
    void openLibraryFolderRequested();
    void addRomFolderRequested();
    void removeRomFolderRequested(const QString& path);
    // A Servers or Input control changed: gui.set_setting with this key
    // and value.
    void settingRequested(const QString& key, const QVariant& value);
    void keyBindingsRequested();

private:
    QListWidget* m_pages;
    QStackedWidget* m_stack;
    QLabel* m_libraryRoot;
    QListWidget* m_romFolders = nullptr;
    QPushButton* m_removeFolder = nullptr;
    struct ServerControls {
        QCheckBox* enabled = nullptr;
        QSpinBox* port = nullptr;
        QLabel* address = nullptr;
        QPushButton* copy = nullptr;
        QString token;
    };
    ServerControls m_api;
    ServerControls m_mcp;
    QLabel* m_mcpTokenFile = nullptr;
    QWidget* buildServersPage(QWidget* parent);
    QWidget* buildInputPage(QWidget* parent);
    QWidget* buildVideoPage(QWidget* parent);
    QWidget* buildAudioPage(QWidget* parent);
    QWidget* buildEmulationPage(QWidget* parent);
    QComboBox* m_device = nullptr;
    QSlider* m_volume = nullptr;
    QCheckBox* m_muted = nullptr;
    QSpinBox* m_latency = nullptr;
    QComboBox* m_ffAudio = nullptr;
    QCheckBox* m_pauseInactive = nullptr;
    QCheckBox* m_resumeOnOpen = nullptr;
    QComboBox* m_ffSpeed = nullptr;
    struct VideoControls {
        QSpinBox* scale = nullptr;
        QCheckBox* fullscreen = nullptr;
        QCheckBox* integerScaling = nullptr;
        QComboBox* aspect = nullptr;
        QSpinBox* overscan[4] = {};
        QComboBox* filter = nullptr;
        QComboBox* pacing = nullptr;
        QLabel* pacingNow = nullptr;
        QCheckBox* alwaysOnTop = nullptr;
        QCheckBox* showFps = nullptr;
        QCheckBox* showOsd = nullptr;
    };
    VideoControls m_video;
    QSpinBox* m_turboRate = nullptr;
    QCheckBox* m_opposite = nullptr;
    QCheckBox* m_pauseOnDisconnect = nullptr;
    QLabel* m_gamepads = nullptr;
};

} // namespace cutenes::app
