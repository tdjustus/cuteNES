#include <cutenes/qt/input_mapper.hpp>

#include <Qt>

namespace cutenes::qt {

namespace {

int portIndex(Port port)
{
    return port == Port::One ? 0 : 1;
}

std::size_t bit(Button b)
{
    return static_cast<std::size_t>(b);
}

// The D-pad direction the left stick stands in for.
PadControl dpadFor(PadControl control)
{
    switch (control) {
    case PadControl::LeftStickUp: return PadControl::DPadUp;
    case PadControl::LeftStickDown: return PadControl::DPadDown;
    case PadControl::LeftStickLeft: return PadControl::DPadLeft;
    case PadControl::LeftStickRight: return PadControl::DPadRight;
    default: return control;
    }
}

} // namespace

InputMapper::InputMapper()
{
    bind(Qt::Key_Up, Port::One, Button::Up);
    bind(Qt::Key_Down, Port::One, Button::Down);
    bind(Qt::Key_Left, Port::One, Button::Left);
    bind(Qt::Key_Right, Port::One, Button::Right);
    bind(Qt::Key_X, Port::One, Button::A);
    bind(Qt::Key_Z, Port::One, Button::B);
    // Qt reports both Shift keys as Key_Shift, and Return and keypad Enter
    // as two keys; each pair acts as one.
    bind(Qt::Key_Shift, Port::One, Button::Select);
    bind(Qt::Key_Return, Port::One, Button::Start);
    bind(Qt::Key_Enter, Port::One, Button::Start);
}

void InputMapper::bind(int key, Port port, Button button, bool turbo)
{
    m_keys[key] = {port, button, turbo};
}

void InputMapper::bindPad(Port port, PadControl control, Button button, bool turbo)
{
    m_pads[{portIndex(port), static_cast<int>(control)}] = {port, button, turbo};
}

void InputMapper::unbindAll()
{
    m_keys.clear();
    m_pads.clear();
    m_held.clear();
}

InputMapper::Source InputMapper::padSource(Port port, PadControl control)
{
    return {-1 - portIndex(port), static_cast<int>(control)};
}

const InputMapper::Target* InputMapper::targetOf(const Source& source) const
{
    if (source.first >= 0) {
        const auto it = m_keys.find(source.first);
        return it == m_keys.end() ? nullptr : &it->second;
    }
    const int port = -1 - source.first;
    auto it = m_pads.find({port, source.second});
    if (it == m_pads.end()) {
        const auto stood = dpadFor(static_cast<PadControl>(source.second));
        it = m_pads.find({port, static_cast<int>(stood)});
    }
    return it == m_pads.end() ? nullptr : &it->second;
}

bool InputMapper::press(const Source& source, bool pressed)
{
    if (!targetOf(source)) return false;
    if (pressed) {
        if (!m_held.count(source)) m_held[source] = ++m_presses;
    } else {
        m_held.erase(source);
    }
    return true;
}

bool InputMapper::handleKey(int key, bool pressed)
{
    return press({key, -1}, pressed);
}

bool InputMapper::handlePad(Port port, PadControl control, bool pressed)
{
    return press(padSource(port, control), pressed);
}

void InputMapper::releasePad(Port port)
{
    const int tag = -1 - portIndex(port);
    for (auto it = m_held.begin(); it != m_held.end();) {
        it = it->first.first == tag ? m_held.erase(it) : std::next(it);
    }
}

void InputMapper::clear()
{
    m_held.clear();
}

ButtonSet InputMapper::buttons(Port port) const
{
    ButtonSet b;
    std::uint64_t latest[8] = {};
    for (const auto& [source, order] : m_held) {
        const Target* t = targetOf(source);
        if (!t || t->port != port || t->turbo) continue;
        b.set(bit(t->button));
        latest[bit(t->button)] = std::max(latest[bit(t->button)], order);
    }
    if (!m_allowOpposite) {
        // The most recent of each pair wins, as a real D-pad allows only one.
        for (const auto& [x, y] : {std::pair{Button::Left, Button::Right}, std::pair{Button::Up, Button::Down}}) {
            if (b.test(bit(x)) && b.test(bit(y))) b.reset(bit(latest[bit(x)] > latest[bit(y)] ? y : x));
        }
    }
    return b;
}

ButtonSet InputMapper::turbo(Port port) const
{
    ButtonSet b;
    for (const auto& [source, order] : m_held) {
        const Target* t = targetOf(source);
        if (t && t->port == port && t->turbo) b.set(bit(t->button));
    }
    return b;
}

} // namespace cutenes::qt
