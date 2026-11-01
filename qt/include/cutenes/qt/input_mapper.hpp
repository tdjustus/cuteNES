#pragma once

// Keyboard keys to NES buttons (spec FR-032, contracts/menus-and-commands.md
// "Game input defaults"). The window rebinds it from the Key Bindings page;
// gamepads arrive in G4.

#include <cutenes/controller.hpp>

#include <map>
#include <set>

namespace cutenes::qt {

class InputMapper {
public:
    // The contract's player-1 keyboard defaults.
    InputMapper();

    // A key went down or up (Qt::Key values; auto-repeats are the caller's
    // to drop). Returns true when `key` is bound, so the caller can keep it
    // from other widgets.
    bool handleKey(int key, bool pressed);
    bool isBound(int key) const { return m_bindings.count(key) != 0; }
    // Releases everything, as when the window loses focus.
    void clear();

    ButtonSet buttons(Port port) const;
    void bind(int key, Port port, Button button);
    // Forgets every binding (and what is held), before a rebind.
    void unbindAll();

private:
    struct Target {
        Port port;
        Button button;
    };
    std::map<int, Target> m_bindings;
    std::set<int> m_held;
};

} // namespace cutenes::qt
