#pragma once

// Gamepads (spec US5, FR-032, research R5). SDL3's gamepad API gives every
// supported pad the standard layout; the backend turns its events into
// digital controls, and GamepadPorts gives each pad a player. The window
// polls the backend on the GUI thread; tests drive a fake one.

#include <cutenes/controller.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace cutenes::qt {

// A pad's digital controls. The stick directions and the triggers are
// buttons past a dead zone (AxisButtons).
enum class PadControl : std::uint8_t {
    South, East, West, North, // Xbox A, B, X, Y
    Back, Guide, Start,
    LeftStick, RightStick, LeftShoulder, RightShoulder,
    DPadUp, DPadDown, DPadLeft, DPadRight,
    LeftTrigger, RightTrigger,
    LeftStickUp, LeftStickDown, LeftStickLeft, LeftStickRight,
    RightStickUp, RightStickDown, RightStickLeft, RightStickRight,
    Count,
};

const char* padControlName(PadControl control);
std::optional<PadControl> padControlFromName(const std::string& name);

enum class PadAxis : std::uint8_t { LeftX, LeftY, RightX, RightY, LeftTrigger, RightTrigger };

struct PadControlEvent {
    PadControl control;
    bool pressed;
};

// Axis positions (-1..1, triggers 0..1) to stick and trigger buttons: a
// press past 0.5, a release below 0.35.
class AxisButtons {
public:
    std::vector<PadControlEvent> feed(PadAxis axis, float value);

private:
    std::array<bool, static_cast<std::size_t>(PadControl::Count)> m_down{};
};

struct PadEvent {
    enum class Kind { Added, Removed, Control } kind;
    std::uint32_t id = 0; // the pad while it is plugged in
    std::string guid;     // Added: the same for the same model and port
    std::string name;     // Added
    PadControl control = PadControl::South; // Control
    bool pressed = false;                   // Control
};

class GamepadBackend {
public:
    virtual ~GamepadBackend() = default;
    // The events since the last call, oldest first. GUI thread.
    virtual std::vector<PadEvent> poll() = 0;
};

// SDL3's gamepads. Needs a MediaRuntime with gamepads; otherwise it never
// reports a pad.
std::unique_ptr<GamepadBackend> makeSdlGamepads();

// Which player each pad drives: the first pad player 1, the second player 2.
// A port stays with its pad's GUID while the pad is away, so a re-plugged
// pad gets its player back; another pad takes the port only when no free
// port is left for it.
class GamepadPorts {
public:
    std::optional<Port> added(std::uint32_t id, const std::string& guid);
    std::optional<Port> removed(std::uint32_t id);
    std::optional<Port> portOf(std::uint32_t id) const;
    // The GUID kept for a port (the settings store it across launches).
    std::string guidOf(Port port) const;
    void remember(Port port, const std::string& guid);

private:
    static std::size_t index(Port port) { return port == Port::One ? 0 : 1; }
    std::array<std::optional<std::uint32_t>, 2> m_pad;
    std::array<std::string, 2> m_guid;
};

} // namespace cutenes::qt
