#pragma once

// The main window (spec US1-US3, contracts/menus-and-commands.md). Every
// menu item of the contract is present from G0 on; an item stays disabled
// until the phase that implements it (tasks G034).
//
// The window hosts the one api Session (research R3): the GUI, the API and
// MCP share its queue. Every implemented menu action runs its gui.* command
// through that queue (R11). It shows the library browser when no game is
// open, and the game otherwise.

#include "host/gui_host.hpp"
#include "library/library.hpp"
#include "persistence/battery_saver.hpp"
#include "settings/app_settings.hpp"

#include <cutenes/api/session.hpp>
#include <cutenes/qt/input_mapper.hpp>

#include <QFuture>
#include <QMainWindow>
#include <QPointer>
#include <QThreadPool>
#include <QVariantMap>

#include <chrono>
#include <memory>
#include <mutex>

class QMenu;
class QStackedWidget;

namespace cutenes::qt {
class AudioOutput;
class EmulationView;
class FrameClock;
} // namespace cutenes::qt

namespace cutenes::app {

class LibraryBrowser;
class LibraryIndex;
class SettingsDialog;

struct MainWindowOptions {
    QString settingsFile = AppSettings::defaultFile();
    // --library DIR: this run only; empty uses the saved setting.
    QString libraryRoot;
    QString indexCacheFile; // empty: LibraryIndex::defaultCacheFile()
    std::chrono::milliseconds batteryInterval = std::chrono::seconds(5);
    // Open Library Folder starts the file manager; tests turn it off.
    bool openExternally = true;
};

class MainWindow : public QMainWindow, private GuiHost::Window {
    Q_OBJECT

public:
    using Options = MainWindowOptions;

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

    // Runs a command on the session without blocking the GUI thread. A
    // failure is shown to the user unless `quiet`.
    QFuture<api::Result> runCommand(const QString& name, const QVariantMap& args = {},
        bool quiet = false);

    // Says so when the library folder is missing or not writable, and offers
    // to recreate it or choose another (spec US2-5). The application calls
    // it once the window is shown.
    void checkLibrary();

public slots:
    void showAbout() override;
    void showOpenRomDialog() override;
    void openRom(const QString& path);
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
    // A warning was shown to the user (a battery save set aside, …).
    void warningShown(const QString& message);
    // Open Library Folder ran (whether or not the file manager was started).
    void libraryFolderOpened(const QString& path);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void changeEvent(QEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    // GuiHost::Window, called on the emulation thread.
    void showLibrary(const QString& select, bool paused) override;
    void showSettings(const QString& page) override;
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
    void wireActions();
    void rebuildRecentMenu();
    void setRunState(const QString& runState);    // GUI thread
    void showGame();
    void returnToGame();
    void pushInput();
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
    qt::EmulationView* m_view = nullptr;
    LibraryIndex* m_index = nullptr;
    QMenu* m_recentMenu = nullptr;
    QAction* m_pauseAction = nullptr;
    QPointer<SettingsDialog> m_settingsDialog;
    std::unique_ptr<api::Session> m_session;
    std::unique_ptr<qt::FrameClock> m_clock;
    qt::InputMapper m_input;
    QThreadPool m_commands;
    QString m_romName;
    QString m_runState = QStringLiteral("NeedsRom");
    bool m_resumeOnReturn = false;
    bool m_closing = false;
};

} // namespace cutenes::app
