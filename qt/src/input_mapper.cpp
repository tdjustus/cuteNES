#include <cutenes/qt/input_mapper.hpp>

#include <Qt>

namespace cutenes::qt {

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

void InputMapper::bind(int key, Port port, Button button)
{
    m_bindings[key] = {port, button};
}

void InputMapper::unbindAll()
{
    m_bindings.clear();
    m_held.clear();
}

bool InputMapper::handleKey(int key, bool pressed)
{
    if (!m_bindings.count(key)) {
        return false;
    }
    if (pressed) {
        m_held.insert(key);
    } else {
        m_held.erase(key);
    }
    return true;
}

void InputMapper::clear()
{
    m_held.clear();
}

ButtonSet InputMapper::buttons(Port port) const
{
    ButtonSet b;
    for (int key : m_held) {
        const Target& t = m_bindings.at(key);
        if (t.port == port) {
            b.set(static_cast<std::size_t>(t.button));
        }
    }
    return b;
}

} // namespace cutenes::qt
