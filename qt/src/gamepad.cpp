#include <cutenes/qt/gamepad.hpp>

#include <SDL3/SDL.h>

#include <map>

namespace cutenes::qt {

namespace {

constexpr const char* kNames[] = {
    "South", "East", "West", "North", "Back", "Guide", "Start",
    "LeftStick", "RightStick", "LeftShoulder", "RightShoulder",
    "DPadUp", "DPadDown", "DPadLeft", "DPadRight",
    "LeftTrigger", "RightTrigger",
    "LeftStickUp", "LeftStickDown", "LeftStickLeft", "LeftStickRight",
    "RightStickUp", "RightStickDown", "RightStickLeft", "RightStickRight",
};
static_assert(std::size(kNames) == static_cast<std::size_t>(PadControl::Count));

constexpr float kPress = 0.5f;
constexpr float kRelease = 0.35f;

class SdlGamepads final : public GamepadBackend {
public:
    ~SdlGamepads() override
    {
        for (auto& [id, pad] : m_open) SDL_CloseGamepad(pad.handle);
    }

    std::vector<PadEvent> poll() override
    {
        std::vector<PadEvent> out;
        if (!SDL_WasInit(SDL_INIT_GAMEPAD)) return out;
        SDL_UpdateGamepads();
        SDL_Event e;
        // Only the gamepad events: audio device events stay for SDL.
        while (SDL_PeepEvents(&e, 1, SDL_GETEVENT, SDL_EVENT_GAMEPAD_AXIS_MOTION, SDL_EVENT_GAMEPAD_UPDATE_COMPLETE) > 0) {
            switch (e.type) {
            case SDL_EVENT_GAMEPAD_ADDED: add(e.gdevice.which, out); break;
            case SDL_EVENT_GAMEPAD_REMOVED: remove(e.gdevice.which, out); break;
            case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            case SDL_EVENT_GAMEPAD_BUTTON_UP:
                if (e.gbutton.button < SDL_GAMEPAD_BUTTON_MISC1 && m_open.count(e.gbutton.which)) {
                    out.push_back({PadEvent::Kind::Control, e.gbutton.which, {}, {},
                        static_cast<PadControl>(e.gbutton.button), e.gbutton.down});
                }
                break;
            case SDL_EVENT_GAMEPAD_AXIS_MOTION:
                if (auto it = m_open.find(e.gaxis.which); it != m_open.end() && e.gaxis.axis < SDL_GAMEPAD_AXIS_COUNT) {
                    const float value = e.gaxis.value < 0 ? e.gaxis.value / 32768.0f : e.gaxis.value / 32767.0f;
                    for (const PadControlEvent& c : it->second.axes.feed(static_cast<PadAxis>(e.gaxis.axis), value)) {
                        out.push_back({PadEvent::Kind::Control, e.gaxis.which, {}, {}, c.control, c.pressed});
                    }
                }
                break;
            default:
                break;
            }
        }
        // The joystick events behind them are not read: they would fill
        // SDL's queue.
        SDL_FlushEvents(SDL_EVENT_JOYSTICK_AXIS_MOTION, SDL_EVENT_JOYSTICK_UPDATE_COMPLETE);
        return out;
    }

private:
    struct Open {
        SDL_Gamepad* handle;
        AxisButtons axes;
    };

    void add(SDL_JoystickID id, std::vector<PadEvent>& out)
    {
        if (m_open.count(id)) return;
        SDL_Gamepad* pad = SDL_OpenGamepad(id);
        if (!pad) return;
        m_open[id] = {pad, {}};
        char guid[33] = {};
        SDL_GUIDToString(SDL_GetGamepadGUIDForID(id), guid, sizeof guid);
        const char* name = SDL_GetGamepadName(pad);
        out.push_back({PadEvent::Kind::Added, id, guid, name ? name : "Gamepad", PadControl::South, false});
    }

    void remove(SDL_JoystickID id, std::vector<PadEvent>& out)
    {
        const auto it = m_open.find(id);
        if (it == m_open.end()) return;
        SDL_CloseGamepad(it->second.handle);
        m_open.erase(it);
        out.push_back({PadEvent::Kind::Removed, id, {}, {}, PadControl::South, false});
    }

    std::map<SDL_JoystickID, Open> m_open;
};

} // namespace

const char* padControlName(PadControl control)
{
    const auto i = static_cast<std::size_t>(control);
    return i < std::size(kNames) ? kNames[i] : "";
}

std::optional<PadControl> padControlFromName(const std::string& name)
{
    for (std::size_t i = 0; i < std::size(kNames); ++i) {
        if (name == kNames[i]) return static_cast<PadControl>(i);
    }
    return std::nullopt;
}

std::vector<PadControlEvent> AxisButtons::feed(PadAxis axis, float value)
{
    // Each axis is two buttons (a stick's directions) or one (a trigger).
    PadControl negative = PadControl::Count;
    PadControl positive = PadControl::Count;
    switch (axis) {
    case PadAxis::LeftX: negative = PadControl::LeftStickLeft; positive = PadControl::LeftStickRight; break;
    case PadAxis::LeftY: negative = PadControl::LeftStickUp; positive = PadControl::LeftStickDown; break;
    case PadAxis::RightX: negative = PadControl::RightStickLeft; positive = PadControl::RightStickRight; break;
    case PadAxis::RightY: negative = PadControl::RightStickUp; positive = PadControl::RightStickDown; break;
    case PadAxis::LeftTrigger: positive = PadControl::LeftTrigger; break;
    case PadAxis::RightTrigger: positive = PadControl::RightTrigger; break;
    }
    std::vector<PadControlEvent> out;
    const auto update = [&](PadControl control, float amount) {
        if (control == PadControl::Count) return;
        bool& down = m_down[static_cast<std::size_t>(control)];
        const bool now = down ? amount > kRelease : amount > kPress;
        if (now != down) {
            down = now;
            out.push_back({control, now});
        }
    };
    // Releases first, so a stick swung across reports left up, then right down.
    const float neg = -value;
    if (neg <= value) {
        update(negative, neg);
        update(positive, value);
    } else {
        update(positive, value);
        update(negative, neg);
    }
    return out;
}

std::unique_ptr<GamepadBackend> makeSdlGamepads()
{
    return std::make_unique<SdlGamepads>();
}

std::optional<Port> GamepadPorts::added(std::uint32_t id, const std::string& guid)
{
    const Port ports[] = {Port::One, Port::Two};
    std::optional<Port> chosen;
    // Its own port, if free; then a free port no away pad is kept for;
    // then any free port.
    for (Port p : ports) {
        if (!m_pad[index(p)] && m_guid[index(p)] == guid) chosen = chosen ? chosen : p;
    }
    for (Port p : ports) {
        if (!chosen && !m_pad[index(p)] && m_guid[index(p)].empty()) chosen = p;
    }
    for (Port p : ports) {
        if (!chosen && !m_pad[index(p)]) chosen = p;
    }
    if (!chosen) return std::nullopt;
    m_pad[index(*chosen)] = id;
    m_guid[index(*chosen)] = guid;
    return chosen;
}

std::optional<Port> GamepadPorts::removed(std::uint32_t id)
{
    const std::optional<Port> port = portOf(id);
    if (port) m_pad[index(*port)].reset(); // the GUID stays
    return port;
}

std::optional<Port> GamepadPorts::portOf(std::uint32_t id) const
{
    for (Port p : {Port::One, Port::Two}) {
        if (m_pad[index(p)] == id) return p;
    }
    return std::nullopt;
}

std::string GamepadPorts::guidOf(Port port) const
{
    return m_guid[index(port)];
}

void GamepadPorts::remember(Port port, const std::string& guid)
{
    m_guid[index(port)] = guid;
}

} // namespace cutenes::qt
