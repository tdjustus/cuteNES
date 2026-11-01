// G040 (spec US5, FR-032, research R5): the input logic below the window.
// - Opposite directions: with "allow opposite directions" off (the
//   default), the most recent of Left/Right and of Up/Down wins.
// - Turbo buttons are reported apart from held ones (the session toggles
//   them on its frame counter).
// - Gamepads: the first one goes to player 1 and the second to player 2,
//   and a re-plugged pad gets its port back by GUID; the left stick works
//   as the D-pad past a dead zone; a pad's controls bind per player.

#include <cutenes/qt/gamepad.hpp>
#include <cutenes/qt/input_mapper.hpp>

#include <Qt>

#include <gtest/gtest.h>

using namespace cutenes::qt;
using cutenes::Button;
using cutenes::ButtonSet;
using cutenes::Port;

namespace {

ButtonSet set(std::initializer_list<Button> buttons)
{
    ButtonSet b;
    for (Button button : buttons) b.set(static_cast<std::size_t>(button));
    return b;
}

} // namespace

TEST(GuiInputLogic, TheLatestOppositeDirectionWins)
{
    InputMapper m;
    m.handleKey(Qt::Key_Left, true);
    m.handleKey(Qt::Key_Right, true);
    EXPECT_EQ(m.buttons(Port::One), set({Button::Right})) << "Right came last";
    m.handleKey(Qt::Key_Right, false);
    EXPECT_EQ(m.buttons(Port::One), set({Button::Left})) << "Left is still held";
    m.handleKey(Qt::Key_Down, true);
    m.handleKey(Qt::Key_Up, true);
    m.handleKey(Qt::Key_Down, false);
    m.handleKey(Qt::Key_Down, true);
    EXPECT_EQ(m.buttons(Port::One), set({Button::Left, Button::Down}));

    m.setAllowOppositeDirections(true);
    EXPECT_EQ(m.buttons(Port::One), set({Button::Left, Button::Up, Button::Down}));
}

TEST(GuiInputLogic, TurboButtonsAreReportedApart)
{
    InputMapper m;
    m.bind(Qt::Key_S, Port::One, Button::A, /*turbo=*/true);
    m.handleKey(Qt::Key_S, true);
    m.handleKey(Qt::Key_Z, true);
    EXPECT_EQ(m.turbo(Port::One), set({Button::A}));
    EXPECT_EQ(m.buttons(Port::One), set({Button::B}));
    m.handleKey(Qt::Key_S, false);
    EXPECT_TRUE(m.turbo(Port::One).none());
}

TEST(GuiInputLogic, GamepadControlsBindPerPlayerAndCombineWithKeys)
{
    InputMapper m;
    m.bindPad(Port::One, PadControl::South, Button::A);
    m.bindPad(Port::Two, PadControl::South, Button::A);
    m.bindPad(Port::One, PadControl::DPadLeft, Button::Left);
    EXPECT_TRUE(m.handlePad(Port::Two, PadControl::South, true));
    EXPECT_EQ(m.buttons(Port::Two), set({Button::A}));
    EXPECT_TRUE(m.buttons(Port::One).none()) << "player 2's pad drives player 2";
    m.handlePad(Port::One, PadControl::South, true);
    m.handleKey(Qt::Key_X, true);
    m.handlePad(Port::One, PadControl::South, false);
    EXPECT_EQ(m.buttons(Port::One), set({Button::A})) << "the key still holds A (spec: inputs combine)";

    // The left stick is the D-pad unless it has bindings of its own.
    EXPECT_TRUE(m.handlePad(Port::One, PadControl::LeftStickLeft, true));
    EXPECT_TRUE(m.buttons(Port::One).test(static_cast<std::size_t>(Button::Left)));
    m.handlePad(Port::One, PadControl::LeftStickLeft, false);
    EXPECT_FALSE(m.handlePad(Port::One, PadControl::Guide, true)) << "unbound";

    // Unplugging releases what that pad held.
    m.handlePad(Port::Two, PadControl::South, true);
    m.releasePad(Port::Two);
    EXPECT_TRUE(m.buttons(Port::Two).none());
}

TEST(GuiInputLogic, PadControlNamesRoundTrip)
{
    for (int i = 0; i < static_cast<int>(PadControl::Count); ++i) {
        const auto c = static_cast<PadControl>(i);
        EXPECT_EQ(padControlFromName(padControlName(c)), c) << padControlName(c);
    }
    EXPECT_EQ(padControlName(PadControl::South), std::string("South"));
    EXPECT_FALSE(padControlFromName("Nope"));
}

// The left stick and the triggers become buttons past a dead zone, with
// hysteresis so a stick resting near the edge does not chatter.
TEST(GuiInputLogic, SticksAndTriggersBecomeButtonsPastADeadZone)
{
    AxisButtons axes;
    EXPECT_TRUE(axes.feed(PadAxis::LeftX, -0.3f).empty()) << "inside the dead zone";
    auto events = axes.feed(PadAxis::LeftX, -0.6f);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].control, PadControl::LeftStickLeft);
    EXPECT_TRUE(events[0].pressed);
    EXPECT_TRUE(axes.feed(PadAxis::LeftX, -0.45f).empty()) << "hysteresis: still pressed";
    events = axes.feed(PadAxis::LeftX, 0.7f);
    ASSERT_EQ(events.size(), 2u) << "left released, right pressed";
    EXPECT_EQ(events[0].control, PadControl::LeftStickLeft);
    EXPECT_FALSE(events[0].pressed);
    EXPECT_EQ(events[1].control, PadControl::LeftStickRight);
    EXPECT_TRUE(events[1].pressed);
    events = axes.feed(PadAxis::LeftY, 0.8f); // SDL: positive Y is down
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].control, PadControl::LeftStickDown);
    events = axes.feed(PadAxis::RightTrigger, 0.9f);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].control, PadControl::RightTrigger);
}

TEST(GuiInputLogic, PadsGetPortsInOrderAndKeepThemByGuid)
{
    GamepadPorts ports;
    EXPECT_EQ(ports.added(10, "guid-a"), Port::One);
    EXPECT_EQ(ports.added(11, "guid-b"), Port::Two);
    EXPECT_EQ(ports.added(12, "guid-c"), std::nullopt) << "two players, two pads";
    EXPECT_EQ(ports.removed(10), Port::One);
    EXPECT_EQ(ports.portOf(11), Port::Two);
    // Pad A comes back (a new instance id, the same GUID): player 1 again.
    EXPECT_EQ(ports.added(13, "guid-a"), Port::One);

    // A port is kept for its pad's GUID while it is away: another pad takes
    // it only when no free port is left for it.
    GamepadPorts again;
    again.added(1, "guid-a");
    again.added(2, "guid-b");
    again.removed(1);
    again.removed(2);
    EXPECT_EQ(again.added(3, "guid-b"), Port::Two);
    EXPECT_EQ(again.added(4, "guid-c"), Port::One) << "the only free port";
    EXPECT_EQ(again.added(5, "guid-a"), std::nullopt);

    // Remembered across launches (the settings hold the GUIDs).
    GamepadPorts restored;
    restored.remember(Port::Two, "guid-z");
    EXPECT_EQ(restored.added(7, "guid-y"), Port::One);
    EXPECT_EQ(restored.added(8, "guid-z"), Port::Two);
    EXPECT_EQ(restored.guidOf(Port::Two), "guid-z");
}
