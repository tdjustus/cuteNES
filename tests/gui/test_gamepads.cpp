// G040-G042 (spec US5-1..5, FR-032, FR-036, US1-2 gamepad focus; research
// R5): gamepads in the window, through a fake backend in place of SDL.
// - The first pad drives player 1 and the second player 2, with the
//   contract's defaults; North and East are turbo A and B.
// - Unplugging a pad during play pauses (the setting, on by default) and
//   says which player lost its controller.
// - The Key Bindings page binds a pad control by pressing it, and a pad
//   combination works as a hotkey.
// - The library is reachable with a pad alone.
// - Settings > Input holds the turbo rate, opposite directions and the
//   disconnect pause, and lists the pads.

#include "gui_support.hpp"
#include "library/library_browser.hpp"
#include "mainwindow.hpp"

#include <cutenes/qt/emulation_view.hpp>
#include <cutenes/qt/gamepad.hpp>

#include <QAbstractItemView>
#include <QApplication>
#include <QAbstractButton>
#include <QCheckBox>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QKeyEvent>
#include <QSpinBox>
#include <QTreeWidget>

#include <gtest/gtest.h>

#include <deque>
#include <mutex>

using cutenes::api::Json;
using cutenes::api::Result;
using cutenes::app::MainWindow;
using cutenes::qt::PadControl;
using cutenes::qt::PadEvent;
using cutenes::test::waitUntil;
using cutenes::test::windowOptions;

namespace {

const QString kRom = QStringLiteral(CUTENES_TEST_ROMS "/instr_test-v5/rom_singles/01-basics.nes");

// Pads the test plugs in and presses; the window polls it like SDL.
class FakeGamepads final : public cutenes::qt::GamepadBackend {
public:
    void plug(std::uint32_t id, const std::string& guid, const std::string& name)
    {
        push({PadEvent::Kind::Added, id, guid, name, PadControl::South, false});
    }
    void unplug(std::uint32_t id) { push({PadEvent::Kind::Removed, id, {}, {}, PadControl::South, false}); }
    void press(std::uint32_t id, PadControl c, bool down = true) { push({PadEvent::Kind::Control, id, {}, {}, c, down}); }
    void release(std::uint32_t id, PadControl c) { press(id, c, false); }

    std::vector<PadEvent> poll() override
    {
        std::lock_guard lock(m_mutex);
        std::vector<PadEvent> out(m_events.begin(), m_events.end());
        m_events.clear();
        return out;
    }

private:
    void push(const PadEvent& e)
    {
        std::lock_guard lock(m_mutex);
        m_events.push_back(e);
    }
    std::mutex m_mutex;
    std::deque<PadEvent> m_events;
};

Json call(MainWindow& w, const char* name, const Json& args = Json::object())
{
    const Result r = w.session().call(name, args);
    EXPECT_FALSE(r.isError()) << name << ": " << (r.isError() ? r.error->message : "");
    return r.value;
}

Json held(MainWindow& w, int port)
{
    return call(w, "core.get_controller", {{"port", port}})["buttons"];
}

bool holds(MainWindow& w, int port, const char* button)
{
    const Json b = held(w, port);
    return std::find(b.begin(), b.end(), button) != b.end();
}

std::string runState(MainWindow& w)
{
    return call(w, "core.run_state")["runState"].get<std::string>();
}

struct Fixture {
    explicit Fixture(QString* folder = nullptr)
        : pads(std::make_shared<FakeGamepads>()), options(windowOptionsWith(pads, folder)), w(options)
    {
        w.show();
        w.activateWindow();
        waitUntil([&] { return QApplication::activeWindow() == &w; });
    }
    static MainWindow::Options windowOptionsWith(std::shared_ptr<FakeGamepads> pads, QString* folder)
    {
        MainWindow::Options o = windowOptions(folder);
        o.gamepads = std::move(pads);
        return o;
    }
    void openGame()
    {
        ASSERT_FALSE(w.session().call("gui.open_rom", {{"path", kRom.toStdString()}}).isError());
        ASSERT_TRUE(waitUntil([&] { return w.windowTitle().contains(QStringLiteral("01-basics.nes")); }));
    }
    std::shared_ptr<FakeGamepads> pads;
    MainWindow::Options options;
    MainWindow w;
};

} // namespace

TEST(GuiGamepads, ThePadsDrivePlayersOneAndTwoWithTheDefaults)
{
    Fixture f;
    f.openGame();
    f.pads->plug(1, "guid-one", "Pad One");
    f.pads->plug(2, "guid-two", "Pad Two");
    ASSERT_TRUE(waitUntil([&] { return call(f.w, "gui.list_gamepads")["gamepads"].size() == 2; }));
    const Json pads = call(f.w, "gui.list_gamepads")["gamepads"];
    EXPECT_EQ(pads[0]["port"], 1);
    EXPECT_EQ(pads[0]["name"], "Pad One");
    EXPECT_EQ(pads[1]["port"], 2);

    const struct {
        PadControl control;
        const char* button;
    } defaults[] = {{PadControl::South, "A"}, {PadControl::West, "B"}, {PadControl::Back, "Select"},
        {PadControl::Start, "Start"}, {PadControl::DPadUp, "Up"}, {PadControl::DPadLeft, "Left"},
        {PadControl::LeftStickRight, "Right"}};
    for (const auto& d : defaults) {
        f.pads->press(1, d.control);
        EXPECT_TRUE(waitUntil([&] { return holds(f.w, 1, d.button); })) << d.button;
        f.pads->release(1, d.control);
        EXPECT_TRUE(waitUntil([&] { return held(f.w, 1).empty(); })) << d.button;
    }
    f.pads->press(2, PadControl::South);
    EXPECT_TRUE(waitUntil([&] { return holds(f.w, 2, "A"); })) << "the second pad is player 2";
    EXPECT_FALSE(holds(f.w, 1, "A"));
    f.pads->release(2, PadControl::South);

    // North is turbo A: held, A comes and goes with the frames.
    f.pads->press(1, PadControl::North);
    bool on = false, off = false;
    waitUntil([&] {
        (holds(f.w, 1, "A") ? on : off) = true;
        return on && off;
    });
    EXPECT_TRUE(on && off) << "turbo A toggles";
    f.pads->release(1, PadControl::North);
}

TEST(GuiGamepads, UnpluggingAPadPausesAndSaysWhichPlayer)
{
    Fixture f;
    f.openGame();
    f.pads->plug(1, "guid-one", "Pad One");
    f.pads->plug(2, "guid-two", "Pad Two");
    ASSERT_TRUE(waitUntil([&] { return call(f.w, "gui.list_gamepads")["gamepads"].size() == 2; }));
    QStringList messages;
    QObject::connect(&f.w, &MainWindow::messageShown, [&](const QString& text) { messages << text; });
    f.pads->unplug(2);
    EXPECT_TRUE(waitUntil([&] { return runState(f.w) == "Paused"; }));
    ASSERT_TRUE(waitUntil([&] { return !messages.isEmpty(); }));
    EXPECT_TRUE(messages.last().contains(QStringLiteral("Player 2"))) << messages.last().toStdString();

    // Plugged back in, it is player 2 again.
    f.pads->plug(3, "guid-two", "Pad Two");
    ASSERT_TRUE(waitUntil([&] { return call(f.w, "gui.list_gamepads")["gamepads"].size() == 2; }));
    EXPECT_EQ(call(f.w, "gui.list_gamepads")["gamepads"][1]["port"], 2);

    // With the setting off, play goes on.
    call(f.w, "gui.set_setting", {{"key", "input.pauseOnGamepadDisconnect"}, {"value", false}});
    call(f.w, "gui.toggle_pause");
    ASSERT_EQ(runState(f.w), "Running");
    f.pads->unplug(1);
    ASSERT_TRUE(waitUntil([&] { return call(f.w, "gui.list_gamepads")["gamepads"].size() == 1; }));
    EXPECT_EQ(runState(f.w), "Running");
}

TEST(GuiGamepads, ThePageBindsAPadControlAndAPadComboIsAHotkey)
{
    Fixture f;
    f.pads->plug(1, "guid-one", "Pad One");
    call(f.w, "gui.show_shortcuts", {{"section", "controls"}});
    auto* page = f.w.findChild<QWidget*>(QStringLiteral("keyBindingsPage"));
    ASSERT_TRUE(waitUntil([&] { return page->isVisible(); }));
    auto* tree = page->findChild<QTreeWidget*>(QStringLiteral("keyBindingsTree"));
    QTreeWidgetItem* b = nullptr;
    QTreeWidgetItem* quickSave = nullptr;
    for (QTreeWidgetItemIterator it(tree); *it; ++it) {
        if ((*it)->data(0, Qt::UserRole) == QStringLiteral("p1.b")) b = *it;
        if ((*it)->data(0, Qt::UserRole) == QStringLiteral("gui.quick_save")) quickSave = *it;
    }
    ASSERT_TRUE(b && quickSave);
    tree->setFocus();

    // A game button takes the first control pressed.
    tree->setCurrentItem(b, 2);
    QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QApplication::sendEvent(tree, &enter);
    ASSERT_TRUE(waitUntil([&] { return b->text(2).contains(QStringLiteral("Press")); }));
    f.pads->press(1, PadControl::RightShoulder);
    f.pads->release(1, PadControl::RightShoulder);
    const auto binding = [&](const char* id) {
        const Json list = call(f.w, "gui.list_bindings")["bindings"];
        for (const Json& e : list) {
            if (e["id"] == id) return e["bindings"];
        }
        return Json();
    };
    EXPECT_TRUE(waitUntil([&] { return binding("p1.b") == Json::array({"Z", "Pad:RightShoulder"}); }))
        << binding("p1.b").dump();

    // A hotkey takes what is held when the first control is let go.
    tree->setCurrentItem(quickSave, 2);
    QApplication::sendEvent(tree, &enter);
    ASSERT_TRUE(waitUntil([&] { return quickSave->text(2).contains(QStringLiteral("Press")); }));
    f.pads->press(1, PadControl::Back);
    f.pads->press(1, PadControl::LeftShoulder);
    f.pads->release(1, PadControl::LeftShoulder);
    f.pads->release(1, PadControl::Back);
    EXPECT_TRUE(waitUntil([&] { return binding("gui.quick_save") == Json::array({"Ctrl+S", "Pad:Back+LeftShoulder"}); }))
        << binding("gui.quick_save").dump();

    // In a game, the combination is Quick Save, which asks for the name.
    page->findChild<QAbstractButton*>(QStringLiteral("keyBindingsBack"))->click();
    f.openGame();
    f.pads->press(1, PadControl::Back);
    f.pads->press(1, PadControl::LeftShoulder);
    f.pads->release(1, PadControl::LeftShoulder);
    f.pads->release(1, PadControl::Back);
    EXPECT_TRUE(waitUntil([&] { return f.w.findChild<QDialog*>(QStringLiteral("saveStateDialog")) != nullptr; }));
}

TEST(GuiGamepads, ThePadAloneReachesTheLibrary)
{
    QString folder;
    Fixture f(&folder);
    const QString roms = folder + QStringLiteral("/library/roms");
    QDir().mkpath(roms);
    for (const char* name : {"a-first.nes", "b-second.nes"}) {
        QFile::copy(kRom, roms + QLatin1Char('/') + QLatin1String(name));
    }
    ASSERT_TRUE(waitUntil([&] { return call(f.w, "gui.library_list")["entries"].size() == 2; }));
    ASSERT_TRUE(waitUntil([&] { return f.w.isShowingLibrary(); }));
    f.pads->plug(1, "guid-one", "Pad One");
    auto* view = f.w.findChild<cutenes::app::LibraryBrowser*>()->view();
    view->setFocus();
    view->setCurrentIndex(view->model()->index(0, 0));
    f.pads->press(1, PadControl::DPadDown);
    f.pads->release(1, PadControl::DPadDown);
    f.pads->press(1, PadControl::DPadRight);
    f.pads->release(1, PadControl::DPadRight);
    EXPECT_TRUE(waitUntil([&] { return view->currentIndex().row() == 1; })) << "the D-pad moves the selection";
    f.pads->press(1, PadControl::South);
    f.pads->release(1, PadControl::South);
    EXPECT_TRUE(waitUntil([&] { return f.w.windowTitle().contains(QStringLiteral("b-second.nes")); }))
        << "the confirm button starts the game";
}

TEST(GuiGamepads, SettingsInputHoldsTheOptionsAndListsThePads)
{
    Fixture f;
    f.pads->plug(1, "guid-one", "Pad One");
    const Json defaults = call(f.w, "gui.get_settings", {{"group", "input"}})["values"]["input"];
    EXPECT_EQ(defaults["turboRate"], 15);
    EXPECT_EQ(defaults["allowOppositeDirections"], false);
    EXPECT_EQ(defaults["pauseOnGamepadDisconnect"], true);
    EXPECT_EQ(defaults["bindings"], Json::object());

    call(f.w, "gui.set_setting", {{"key", "input.turboRate"}, {"value", 30}});
    call(f.w, "gui.set_setting", {{"key", "input.allowOppositeDirections"}, {"value", true}});
    const Result bad = f.w.session().call("gui.set_setting", {{"key", "input.turboRate"}, {"value", 31}});
    ASSERT_TRUE(bad.isError());
    EXPECT_EQ(bad.error->data["path"], "/value");

    EXPECT_EQ(call(f.w, "gui.show_settings", {{"page", "input"}})["page"], "input");
    QWidget* dialog = nullptr;
    ASSERT_TRUE(waitUntil([&] { return (dialog = f.w.findChild<QWidget*>(QStringLiteral("settingsDialog"))) && dialog->isVisible(); }));
    auto* rate = dialog->findChild<QSpinBox*>(QStringLiteral("turboRate"));
    auto* opposite = dialog->findChild<QCheckBox*>(QStringLiteral("allowOppositeDirections"));
    auto* pause = dialog->findChild<QCheckBox*>(QStringLiteral("pauseOnGamepadDisconnect"));
    ASSERT_TRUE(rate && opposite && pause);
    EXPECT_TRUE(waitUntil([&] { return rate->value() == 30 && opposite->isChecked() && pause->isChecked(); }));
    auto* list = dialog->findChild<QWidget*>(QStringLiteral("gamepadList"));
    ASSERT_TRUE(list);
    EXPECT_TRUE(waitUntil([&] { return list->property("text").toString().contains(QStringLiteral("Pad One")); }))
        << list->property("text").toString().toStdString();

    // The dialog's own controls change the settings.
    pause->click();
    EXPECT_TRUE(waitUntil([&] {
        return call(f.w, "gui.get_settings", {{"group", "input"}})["values"]["input"]["pauseOnGamepadDisconnect"] == false;
    }));
}
