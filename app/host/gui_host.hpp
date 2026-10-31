#pragma once

// The GUI's host commands (research R11, contracts/menus-and-commands.md).
// Every menu action triggers one gui.* command, so a click and an AI's call
// run the same code. Handlers run on the session's emulation thread: machine
// work runs inline through core.* commands, and widget work goes through the
// Window interface, which posts it to the GUI thread. Nothing here waits for
// the GUI.

#include "library/library.hpp"
#include "library/library_index.hpp"
#include "persistence/battery_saver.hpp"

#include <cutenes/api/host.hpp>
#include <cutenes/api/registry.hpp>
#include <cutenes/api/session.hpp>

#include <QString>
#include <QVector>

#include <atomic>
#include <string>

namespace cutenes::app {

class AppSettings;

class GuiHost final : public api::IHost {
public:
    // What the commands need from the window. Called on the emulation
    // thread: the show* and notification calls post their widget work and
    // return at once; the getters are thread-safe.
    class Window {
    public:
        virtual ~Window() = default;
        virtual void showOpenRomDialog() = 0;
        // select: a path or empty; paused: the command paused a running game.
        virtual void showLibrary(const QString& select, bool paused) = 0;
        virtual void showSettings(const QString& page) = 0;
        virtual void showAbout() = 0;
        virtual void openLibraryFolder() = 0;
        virtual void exit() = 0;
        virtual Library library() const = 0;
        virtual QVector<LibraryEntry> libraryEntries() const = 0;
        virtual void gameOpened(const QString& path, const GameIdentity& id, const BatterySaver::Opened& battery) = 0;
        virtual void gameClosed() = 0;
        virtual void recentChanged() = 0;
        virtual void libraryChanged(const Library& library) = 0;
        virtual void romFoldersChanged() = 0;
        virtual void sessionEvent(const api::Event& event) = 0;
    };

    GuiHost(Window& window, AppSettings& settings, BatterySaver& battery);

    // The session hosting these commands, once it exists.
    void attach(api::Session& session) { m_session = &session; }

    std::string mode() const override { return "gui"; }
    void registerCommands(api::Registry& registry) override;
    void onSessionEvent(const api::Event& event) override;

private:
    api::Result openRom(api::CallContext& ctx, const QString& path);
    api::Result closeGame(api::CallContext& ctx);
    api::Result setLibraryRoot(api::CallContext& ctx, const QString& root);
    api::Result addRomFolder(const QString& path);
    api::Result removeRomFolder(const QString& path);

    Window& m_window;
    AppSettings& m_settings;
    BatterySaver& m_battery;
    std::atomic<api::Session*> m_session{nullptr}; // set on the GUI thread, read on the emulation thread
    bool m_gameOpen = false; // emulation thread
};

} // namespace cutenes::app
