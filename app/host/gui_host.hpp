#pragma once

// The GUI's host commands (research R11, contracts/menus-and-commands.md).
// Every menu action triggers one gui.* command, so a click and an AI's call
// run the same code. Handlers run on the session's emulation thread: machine
// work runs inline through core.* commands, and widget work goes through the
// hooks, which the window implements by posting to the GUI thread. A hook
// never waits for the GUI.

#include <cutenes/api/host.hpp>
#include <cutenes/api/registry.hpp>

#include <functional>
#include <string>

namespace cutenes::app {

class GuiHost final : public api::IHost {
public:
    struct Hooks {
        std::function<void()> showOpenRomDialog;
        std::function<void(const std::string& romPath)> gameLoaded;
        std::function<void()> gameClosed;
        std::function<void()> exit;
        std::function<void()> showAbout;
        std::function<void(const api::Event&)> event;
    };

    explicit GuiHost(Hooks hooks);

    std::string mode() const override { return "gui"; }
    void registerCommands(api::Registry& registry) override;
    void onSessionEvent(const api::Event& event) override;

private:
    Hooks m_hooks;
};

} // namespace cutenes::app
