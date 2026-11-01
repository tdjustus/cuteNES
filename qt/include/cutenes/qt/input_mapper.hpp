#pragma once

// Keys and gamepad controls to NES buttons (spec US5, FR-032,
// contracts/menus-and-commands.md "Game input defaults"; research R5). The
// window binds it from the Key Bindings page. A pad's controls bind per
// player: they act for the pad GamepadPorts gave that player. Everything
// held for a player combines; with "allow opposite directions" off, the
// most recent of Left/Right and of Up/Down wins. Turbo buttons are reported
// apart, for the session to toggle on its frame counter.

#include <cutenes/controller.hpp>
#include <cutenes/qt/gamepad.hpp>

#include <cstdint>
#include <map>
#include <utility>

namespace cutenes::qt {

class InputMapper {
public:
    // The contract's player-1 keyboard defaults.
    InputMapper();

    void bind(int key, Port port, Button button, bool turbo = false);
    void bindPad(Port port, PadControl control, Button button, bool turbo = false);
    // Forgets every binding (and what is held), before a rebind.
    void unbindAll();

    // A key went down or up (Qt::Key values; auto-repeats are the caller's
    // to drop). Returns true when `key` is bound, so the caller can keep it
    // from other widgets.
    bool handleKey(int key, bool pressed);
    bool isBound(int key) const { return m_keys.count(key) != 0; }
    // A control of the pad that drives `port`. The left stick acts as the
    // D-pad unless it has bindings of its own. True when bound.
    bool handlePad(Port port, PadControl control, bool pressed);
    // That pad was unplugged: what it held is released.
    void releasePad(Port port);
    // Releases everything, as when the window loses focus.
    void clear();

    void setAllowOppositeDirections(bool allow) { m_allowOpposite = allow; }

    // What `port` holds, opposite directions resolved, turbo buttons apart.
    ButtonSet buttons(Port port) const;
    ButtonSet turbo(Port port) const;

private:
    struct Target {
        Port port;
        Button button;
        bool turbo;
    };
    // A held input: a key, or a pad control of a player's pad.
    using Source = std::pair<int, int>; // {key, -1} or {-1 - port, control}

    static Source padSource(Port port, PadControl control);
    bool press(const Source& source, bool pressed);
    const Target* targetOf(const Source& source) const;

    std::map<int, Target> m_keys;
    std::map<std::pair<int, int>, Target> m_pads; // {port index, control}
    std::map<Source, std::uint64_t> m_held;       // the press order
    std::uint64_t m_presses = 0;
    bool m_allowOpposite = false;
};

} // namespace cutenes::qt
