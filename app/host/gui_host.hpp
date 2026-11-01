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
#include "persistence/state_slots.hpp"

#include <cutenes/api/host.hpp>
#include <cutenes/api/registry.hpp>
#include <cutenes/api/session.hpp>

#include <QString>
#include <QVector>

#include <atomic>
#include <mutex>
#include <optional>
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
        // Save State to File… (save) or Load State from File… without a path.
        virtual void showStateFileDialog(bool save) = 0;
        // A short confirmation for the user ("State saved to slot 3").
        virtual void showMessage(const QString& text) = 0;
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

    // The open game's save-state slots (any thread; empty without a game).
    StateSlots stateSlots() const;
    int selectedSlot() const { return m_selectedSlot.load(); }

    std::string mode() const override { return "gui"; }
    void registerCommands(api::Registry& registry) override;
    void onSessionEvent(const api::Event& event) override;

private:
    api::Result openRom(api::CallContext& ctx, const QString& path);
    api::Result closeGame(api::CallContext& ctx);
    api::Result setLibraryRoot(api::CallContext& ctx, const QString& root);
    api::Result addRomFolder(const QString& path);
    api::Result removeRomFolder(const QString& path);
    api::Result saveState(api::CallContext& ctx, int slot, const QString& name);
    api::Result loadState(api::CallContext& ctx, int slot);
    // Loads `state`, keeping what it replaced for Undo Load State.
    api::Result loadStateBytes(api::CallContext& ctx, const QByteArray& state, bool keepUndo = true);
    api::Result selectSlot(int slot);

    Window& m_window;
    AppSettings& m_settings;
    BatterySaver& m_battery;
    std::atomic<api::Session*> m_session{nullptr}; // set on the GUI thread, read on the emulation thread
    bool m_gameOpen = false; // emulation thread
    GameIdentity m_identity; // emulation thread
    mutable std::mutex m_slotsMutex;
    StateSlots m_slots; // under m_slotsMutex
    std::atomic<int> m_selectedSlot{1};
    std::optional<QByteArray> m_undoState; // emulation thread; one level (data-model "StateSlot")
};

} // namespace cutenes::app
