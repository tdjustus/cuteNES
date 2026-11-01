#pragma once

// The main window (spec US1-US3, contracts/menus-and-commands.md). Every
// menu item of the contract is present from G0 on; an item stays disabled
// until the phase that implements it (tasks G034).
//
// The window hosts the one api Session (research R3): the GUI, the API and
// MCP share its queue. Every implemented menu action runs its gui.* command
// through that queue (R11). It shows the library browser when no game is
// open, and the game otherwise.

#include "dialogs/cheats_window.hpp" // CommandRunner
#include "host/gui_host.hpp"
#include "library/library.hpp"
#include "persistence/battery_saver.hpp"
#include "settings/app_settings.hpp"

#include <cutenes/api/session.hpp>
#include <cutenes/qt/gamepad.hpp>
#include <cutenes/qt/input_mapper.hpp>

#include <QFuture>

#include <atomic>
#include <QMainWindow>
#include <QPointer>
#include <QMultiHash>
#include <QSet>
#include <QThreadPool>
#include <QVariantMap>

#include <array>
#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>

class QLabel;
class QPushButton;
class QMenu;
class QStackedWidget;
class QTimer;

namespace cutenes::qt {
class AudioOutput;
class EmulationView;
class FrameClock;
} // namespace cutenes::qt

namespace cutenes::app {

class CheatSearchWindow;
class DebuggerWindow;
class MemoryViewerWindow;
class PpuViewerWindow;
class TraceLoggerWindow;
class CheatsWindow;
class KeyBindings;
class KeyBindingsPage;
class LibraryBrowser;
class LibraryIndex;
class Servers;
class SettingsDialog;

struct MainWindowOptions {
    QString settingsFile = AppSettings::defaultFile();
    // --library DIR: this run only; empty uses the saved setting.
    QString libraryRoot;
    QString indexCacheFile; // empty: LibraryIndex::defaultCacheFile()
    std::chrono::milliseconds batteryInterval = std::chrono::seconds(5);
    // Open Library Folder starts the file manager; tests turn it off.
    bool openExternally = true;
    // Where the API and MCP token files go; empty: the user's state folder
    // (api::token::defaultDirectory(), which cutenes-server --attach reads).
    QString tokenDir;
    // The gamepads; null: SDL's (tests give a fake).
    std::shared_ptr<qt::GamepadBackend> gamepads;
};

class MainWindow : public QMainWindow, private GuiHost::Window {
    Q_OBJECT

public:
    using Options = MainWindowOptions;
    using Playback = GuiHost::Window::Playback;

    explicit MainWindow(const Options& options = Options{}, QWidget* parent = nullptr);
    ~MainWindow() override;

    // A top-level menu by its object name: "menuFile", "menuEmulation",
    // "menuView", "menuTools", "menuDebug", "menuSettings" or "menuHelp".
    QMenu* menu(const QString& objectName) const;

    api::Session& session() { return *m_session; }
    qt::EmulationView* view() const { return m_view; }
    qt::AudioOutput* audio() const { return m_audio.get(); }
    LibraryBrowser* browser() const { return m_browser; }
    LibraryIndex* libraryIndex() const { return m_index; }
    AppSettings& settings() { return m_settings; }
    Library library() const override;
    bool isShowingLibrary() const;

    // The frame clock's rate now (the console's rate times the speed) and
    // the playback the host last asked for.
    double frameRate() const;
    Playback playback() const;

    // Runs a command on the session without blocking the GUI thread. A
    // failure is shown to the user unless `quiet`.
    QFuture<api::Result> runCommand(const QString& name, const QVariantMap& args = {},
        bool quiet = false);
    // The runner the tool windows get: their repeated reads are quiet.
    CommandRunner toolRunner();

    // Says so when the library folder is missing or not writable, and offers
    // to recreate it or choose another (spec US2-5). The application calls
    // it once the window is shown.
    void checkLibrary();

public slots:
    void showAbout() override;
    void showOpenRomDialog() override;
    void openRom(const QString& path);
    // Save State ▸ Slot N and Quick Save from the menus or keys: pauses the
    // game and asks for the state's name (the time by default), warning
    // when the slot already holds a state. Automation saves without asking.
    void askSaveState(int slot);
    // Asks for a new library folder and whether to move the contents.
    void changeLibraryLocation();
    // Asks for a folder of ROMs and adds it to the library.
    void addRomFolder();
    // Moves the library to `root` (after closing the game when `moveContents`)
    // and makes it the library.
    QFuture<api::Result> relocateLibrary(const QString& root, bool moveContents);

signals:
    // A command the user started failed (after it was shown).
    void commandFailed(const QString& name, const QString& message);
    // A short confirmation was shown ("State saved to slot 3").
    void messageShown(const QString& text);
    // A warning was shown to the user (a battery save set aside, …).
    void warningShown(const QString& message);
    // Open Library Folder ran (whether or not the file manager was started).
    void libraryFolderOpened(const QString& path);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void changeEvent(QEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    // GuiHost::Window, called on the emulation thread.
    void showFileDialog(FileDialog dialog) override;
    void displayChanged() override;
    void videoChanged(bool showPage) override;
    void serversChanged() override;
    void showMessage(const QString& text) override;
    void playbackChanged(const Playback& playback) override;
    PacingNow pacingNow() const override;
    void showCheats(bool search) override;
    void cheatsChanged() override;
    void showMovieDialog(bool record, const QString& from, const QString& folder) override;
    void movieChanged() override;
    void showDebugger() override;
    void showMemoryViewer() override;
    void showPpuViewer(const QString& view) override;
    void showTraceLogger() override;
    QStringList openWindows() const override;
    void closeWindows(const QStringList& names) override;
    QString currentPage() const override;
    void debuggerStopped() override;
    void showLibrary(const QString& select, bool paused) override;
    void showSettings(const QString& page) override;
    void showKeyBindings(const QString& section, bool paused) override;
    KeyBindings& keyBindings() override { return *m_bindings; }
    void bindingsChanged() override;
    QVector<Gamepad> gamepads() const override;
    void inputSettingsChanged() override;
    void audioSettingsChanged() override;
    void offerResume(const QString& autoState) override;
    void recordingChanged(const QString& path) override;
    void openLibraryFolder() override;
    void exit() override;
    QVector<LibraryEntry> libraryEntries() const override;
    void gameOpened(const QString& path, const GameIdentity& id, const BatterySaver::Opened& battery) override;
    void gameClosed() override;
    void recentChanged() override;
    void libraryChanged(const Library& library) override;
    void romFoldersChanged() override;
    void sessionEvent(const api::Event& event) override;

    void createMenus();
    void defineBindings(); // after createMenus: every action and game button
    void applyBindings();  // GUI thread: the menus' shortcuts and the game keys
    void leaveKeyBindings();
    void requestBinding(const QString& id, int index, const QString& binding); // the page's
    void wireActions();
    void rebuildRecentMenu();
    void updateSlotMenus(); // GUI thread: each slot's time and thumbnail
    void showOsd(const QString& text); // GUI thread
    void placeOsd();
    void updateOverlays(); // GUI thread: PAUSED, the speed indicator and the FPS
    void updateFps();      // GUI thread, each second: frames shown in the last one
    void applyServers();   // GUI thread: start or stop the listeners to match the settings
    void updateAutomation(); // GUI thread: the automation indicator
    void releaseHeld(int key); // the release of a hold action's key
    void setRunState(const QString& runState);    // GUI thread
    void showGame();
    void returnToGame();
    void pushInput();
    void applyVideo(); // GUI thread: the video settings to the view, the window and the menus
    void resizeToScale();
    void applyInputSettings(); // GUI thread: the turbo rate and opposite directions
    void applyAudioSettings(); // GUI thread: the device, volume, mute and latency
    void followScreen();       // GUI thread: the display's refresh rate (R4)
    QString pacingText() const; // the pacing in use, for Settings > Video
    void followFocus();        // GUI thread: pause when inactive (FR-036)
    // Gamepads (GUI thread): the backend's events, one control of the pad
    // that drives `port`, and that pad going away.
    void pollGamepads();
    void padControl(Port port, qt::PadControl control, bool pressed);
    void releasePad(Port port);
    void showError(const QString& name, const api::Error& error);
    void showWarning(const QString& message);
    void useLibrary(const Library& library);       // GUI thread
    void updateSettingsDialog();
    bool hasGame() const { return m_runState != QLatin1String("NeedsRom"); }

    Options m_options;
    AppSettings m_settings;
    mutable std::mutex m_libraryMutex;
    Library m_library;
    BatterySaver m_battery;
    std::unique_ptr<GuiHost> m_host;
    std::unique_ptr<qt::AudioOutput> m_audio;
    QStackedWidget* m_pages = nullptr;
    LibraryBrowser* m_browser = nullptr;
    KeyBindingsPage* m_bindingsPage = nullptr;
    QWidget* m_bindingsReturn = nullptr; // the page Back returns to
    std::unique_ptr<KeyBindings> m_bindings;
    QMultiHash<QString, QAction*> m_bindingActions; // by binding id
    qt::EmulationView* m_view = nullptr;
    LibraryIndex* m_index = nullptr;
    QMenu* m_recentMenu = nullptr;
    QAction* m_pauseAction = nullptr;
    QLabel* m_osd = nullptr; // on-screen messages, over the page
    QLabel* m_paused = nullptr; // "PAUSED", in the middle of the game
    QLabel* m_speed = nullptr;  // "◀◀ REWIND", "SPEED 50%"..., top right, in PAUSED's style
    QSet<int> m_keysDown; // keys held now, for the hold actions
    mutable std::mutex m_playbackMutex;
    Playback m_playback; // under m_playbackMutex
    std::atomic<double> m_consoleRate{60.0988}; // the game's region's frame rate
    std::atomic<double> m_displayHz{0};         // the window's screen; 0 when unknown
    PacingNow m_pacing;                         // under m_playbackMutex
    QMetaObject::Connection m_refreshConnection; // the screen's refresh-rate signal
    bool m_screenWatched = false;
    QPointer<CheatsWindow> m_cheatsWindow;           // Tools > Cheats…
    QPointer<CheatSearchWindow> m_cheatSearchWindow; // Tools > Cheat Search…
    QPointer<DebuggerWindow> m_debuggerWindow;
    QPointer<MemoryViewerWindow> m_memoryViewer;
    QPointer<PpuViewerWindow> m_ppuViewer;
    QPointer<TraceLoggerWindow> m_traceLogger;
    // What openWindows() and currentPage() report, kept by the GUI thread
    // as windows show and hide and pages change (updateWindowList).
    void updateWindowList();
    mutable std::mutex m_windowListMutex;
    QStringList m_openWindows;
    QString m_currentPage;
    QTimer* m_osdTimer = nullptr;
    std::unique_ptr<Servers> m_servers; // the API and MCP listeners (Settings > Servers)
    QPushButton* m_automation = nullptr; // bottom right while a client is there; opens Settings > Servers
    QTimer* m_automationTimer = nullptr;
    std::atomic<bool> m_pausedByAutomation{false};
    QLabel* m_fps = nullptr; // "60 FPS", top left, like the speed indicator (Show FPS)
    QLabel* m_movie = nullptr; // "● REC 120", "▶ 120 / 900": under the FPS, while a movie records or plays
    QTimer* m_fpsTimer = nullptr;
    struct FrameSample {
        std::uint64_t frame = 0; // the core's frame counter
        std::chrono::steady_clock::time_point at{};
    };
    FrameSample m_lastFrameEvent; // under m_playbackMutex; from the emulation thread
    FrameSample m_fpsSample;      // GUI thread: at the last count
    QMenu* m_saveStateMenu = nullptr;
    QMenu* m_loadStateMenu = nullptr;
    QMenu* m_selectSlotMenu = nullptr;
    QPointer<SettingsDialog> m_settingsDialog;
    std::unique_ptr<api::Session> m_session;
    std::unique_ptr<qt::FrameClock> m_clock;
    qt::InputMapper m_input;
    int m_turboRate = 15;
    bool m_pausedByFocus = false; // the window paused the game when it lost focus
    std::optional<AppSettings::VideoSettings> m_video; // as applied last
    QTimer* m_menuEdgeTimer = nullptr; // fullscreen without a native menu bar
    std::shared_ptr<qt::GamepadBackend> m_padBackend;
    qt::GamepadPorts m_padPorts;
    QTimer* m_padTimer = nullptr;
    mutable std::mutex m_padMutex;
    std::map<std::uint32_t, Gamepad> m_pads; // by backend id; under m_padMutex
    // The hotkeys bound to pad controls, and per player what its pad holds
    // (in press order), the controls a hotkey took from the game, and the
    // hold actions running until one of their controls is let go.
    struct PadHotkey {
        QVector<qt::PadControl> controls;
        QString id;
    };
    QVector<PadHotkey> m_padHotkeys;
    std::array<QVector<qt::PadControl>, 2> m_padHeld;
    std::array<std::set<qt::PadControl>, 2> m_padTaken;
    struct PadHold {
        Port port;
        QVector<qt::PadControl> controls;
        QString command;
        QVariantMap args;
    };
    QVector<PadHold> m_padHolds;
    QThreadPool m_commands;
    QString m_romName;
    QString m_runState = QStringLiteral("NeedsRom");
    bool m_resumeOnReturn = false;
    bool m_closing = false;
};

} // namespace cutenes::app
