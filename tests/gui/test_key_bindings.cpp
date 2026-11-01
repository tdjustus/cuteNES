// G036 (#108): the Key Bindings page (spec US3-3, US5-2, FR-022, FR-070;
// contracts/menus-and-commands.md "Key Bindings page"). Every menu action
// and game button is listed with its defaults; a binding can be rebound,
// doubled, cleared and reset, by the page or by gui.* commands; conflicts
// are refused unless confirmed; the menus show the current binding, the
// game follows its new keys, and the bindings persist.

#include "gui_support.hpp"
#include "mainwindow.hpp"

#include <cutenes/qt/emulation_view.hpp>

#include <QAction>
#include <QApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QTreeWidget>

#include <gtest/gtest.h>

using cutenes::api::Json;
using cutenes::api::Result;
using cutenes::app::MainWindow;
using cutenes::test::waitUntil;
using cutenes::test::windowOptions;

namespace {

const QString kRom = QStringLiteral(CUTENES_TEST_ROMS "/instr_test-v5/rom_singles/01-basics.nes");

Json call(MainWindow& w, const char* name, const Json& args = Json::object())
{
    const Result r = w.session().call(name, args);
    EXPECT_FALSE(r.isError()) << name << ": " << (r.isError() ? r.error->message : "");
    return r.value;
}

Json entry(MainWindow& w, const std::string& id)
{
    const Json all = call(w, "gui.list_bindings")["bindings"];
    for (const Json& e : all) {
        if (e["id"] == id) return e;
    }
    ADD_FAILURE() << "no entry " << id;
    return Json::object();
}

Json bindings(MainWindow& w, const std::string& id)
{
    return entry(w, id)["bindings"];
}

QAction* action(MainWindow& w, const char* objectName)
{
    for (QAction* a : w.menuBar()->findChildren<QAction*>()) {
        if (a->objectName() == QLatin1String(objectName)) return a;
    }
    return nullptr;
}

QStringList shortcuts(QAction* a)
{
    QStringList texts;
    for (const QKeySequence& k : a->shortcuts()) texts << k.toString(QKeySequence::PortableText);
    return texts;
}

void openGame(MainWindow& w)
{
    ASSERT_FALSE(w.session().call("gui.open_rom", {{"path", kRom.toStdString()}}).isError());
    ASSERT_TRUE(waitUntil([&] { return w.windowTitle().contains(QStringLiteral("01-basics.nes")); }));
}

void key(QWidget* to, QEvent::Type type, int k, Qt::KeyboardModifiers mods = Qt::NoModifier)
{
    QKeyEvent e(type, k, mods);
    QApplication::sendEvent(to, &e);
}

QWidget* page(MainWindow& w)
{
    return w.findChild<QWidget*>(QStringLiteral("keyBindingsPage"));
}

QTreeWidgetItem* row(QTreeWidget* tree, const QString& id)
{
    for (QTreeWidgetItemIterator it(tree); *it; ++it) {
        if ((*it)->data(0, Qt::UserRole).toString() == id) return *it;
    }
    return nullptr;
}

} // namespace

TEST(GuiKeyBindings, EveryHotkeyAndGameButtonIsListedWithItsDefaults)
{
    MainWindow w(windowOptions());
    const Json all = call(w, "gui.list_bindings")["bindings"];
    ASSERT_GT(all.size(), 60u);
    const Json quickSave = entry(w, "gui.quick_save");
    EXPECT_EQ(quickSave["kind"], "hotkey");
    EXPECT_EQ(quickSave["label"], "File > Quick Save");
    EXPECT_EQ(quickSave["bindings"], Json::array({"Ctrl+S", ""}));
    EXPECT_EQ(quickSave["defaults"], quickSave["bindings"]);
    EXPECT_EQ(bindings(w, "gui.save_state.3"), Json::array({"Shift+F3", ""}));
    EXPECT_EQ(entry(w, "gui.save_state.3")["label"], "File > Save State > Slot 3");
    EXPECT_EQ(bindings(w, "gui.toggle_pause"), Json::array({"P", "Pause"}));
    EXPECT_EQ(bindings(w, "gui.set_fast_forward"), Json::array({"Tab", ""})) << "the hold's `on` is not in the id";
    EXPECT_EQ(bindings(w, "gui.set_speed.faster"), Json::array({"=", ""}));
    EXPECT_EQ(bindings(w, "gui.save_state_file"), Json::array({"", ""})) << "actions with no default are listed";
    EXPECT_EQ(bindings(w, "gui.show_settings.video"), Json::array({"", ""}));

    const Json a = entry(w, "p1.a");
    EXPECT_EQ(a["kind"], "control");
    EXPECT_EQ(a["label"], "Player 1 A");
    EXPECT_EQ(a["bindings"], Json::array({"X", "Pad:South"})) << "a key, then the player's pad";
    EXPECT_EQ(bindings(w, "p1.select"), Json::array({"Shift", "Pad:Back"}));
    EXPECT_EQ(bindings(w, "p1.start"), Json::array({"Return", "Pad:Start"})) << "Return stands for keypad Enter too";
    EXPECT_EQ(bindings(w, "p1.turbo_a"), Json::array({"S", "Pad:North"}));
    EXPECT_EQ(bindings(w, "p1.turbo_b"), Json::array({"A", "Pad:East"}));
    EXPECT_EQ(bindings(w, "p2.a"), Json::array({"", "Pad:South"})) << "player 2 has no keyboard defaults";
    for (const Json& e : all) {
        EXPECT_NE(e["id"].get<std::string>().rfind("gui.open_recent", 0), 0u) << "recent games are not bindable";
    }
}

TEST(GuiKeyBindings, ARebindShowsInTheMenuAndPersists)
{
    QString folder;
    const MainWindow::Options options = windowOptions(&folder);
    {
        MainWindow w(options);
        call(w, "gui.set_binding", {{"id", "gui.quick_save"}, {"binding", "Ctrl+Shift+K"}});
        EXPECT_EQ(bindings(w, "gui.quick_save"), Json::array({"Ctrl+Shift+K", ""}));
        EXPECT_TRUE(waitUntil([&] { return shortcuts(action(w, "gui.quick_save")) == QStringList{"Ctrl+Shift+K"}; }))
            << "the menu shows the current binding";
        call(w, "gui.set_binding", {{"id", "p1.a"}, {"binding", "K"}});
    }
    MainWindow again(options);
    EXPECT_EQ(bindings(again, "gui.quick_save"), Json::array({"Ctrl+Shift+K", ""}));
    EXPECT_EQ(shortcuts(action(again, "gui.quick_save")), QStringList{"Ctrl+Shift+K"});
    EXPECT_EQ(bindings(again, "p1.a"), Json::array({"K", "Pad:South"}));
    const Json stored = call(again, "gui.get_settings", {{"group", "hotkeys"}})["values"]["hotkeys"];
    EXPECT_EQ(stored["gui.quick_save"], Json::array({"Ctrl+Shift+K", ""})) << stored.dump();
    const Json input = call(again, "gui.get_settings", {{"group", "input"}})["values"]["input"]["bindings"];
    EXPECT_EQ(input["p1.a"], Json::array({"K", "Pad:South"})) << input.dump();
}

TEST(GuiKeyBindings, ASecondBindingClearAndReset)
{
    MainWindow w(windowOptions());
    call(w, "gui.set_binding", {{"id", "gui.take_screenshot"}, {"binding", "Ctrl+Shift+S"}, {"index", 1}});
    EXPECT_EQ(bindings(w, "gui.take_screenshot"), Json::array({"F12", "Ctrl+Shift+S"}));
    EXPECT_TRUE(waitUntil([&] {
        return shortcuts(action(w, "gui.take_screenshot")) == QStringList({"F12", "Ctrl+Shift+S"});
    }));
    call(w, "gui.clear_binding", {{"id", "gui.take_screenshot"}, {"index", 0}});
    EXPECT_EQ(bindings(w, "gui.take_screenshot"), Json::array({"", "Ctrl+Shift+S"}));
    EXPECT_TRUE(waitUntil([&] { return shortcuts(action(w, "gui.take_screenshot")) == QStringList{"Ctrl+Shift+S"}; }));
    call(w, "gui.reset_bindings", {{"id", "gui.take_screenshot"}});
    EXPECT_EQ(bindings(w, "gui.take_screenshot"), Json::array({"F12", ""}));

    call(w, "gui.clear_binding", {{"id", "gui.toggle_pause"}});
    EXPECT_EQ(bindings(w, "gui.toggle_pause"), Json::array({"", ""}));
    EXPECT_TRUE(waitUntil([&] { return shortcuts(action(w, "gui.toggle_pause")).isEmpty(); }));
    call(w, "gui.set_binding", {{"id", "p1.b"}, {"binding", "J"}});
    const Json reset = call(w, "gui.reset_bindings")["reset"];
    EXPECT_GE(reset.size(), 2u);
    EXPECT_EQ(bindings(w, "gui.toggle_pause"), Json::array({"P", "Pause"}));
    EXPECT_EQ(bindings(w, "p1.b"), Json::array({"Z", "Pad:West"}));
    EXPECT_EQ(call(w, "gui.get_settings", {{"group", "hotkeys"}})["values"]["hotkeys"], Json::object())
        << "nothing differs from the defaults";
}

TEST(GuiKeyBindings, ConflictsAreRefusedUnlessForced)
{
    MainWindow w(windowOptions());
    Result r = w.session().call("gui.set_binding", {{"id", "gui.quick_save"}, {"binding", "Ctrl+L"}});
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(r.error->name, "InvalidArguments");
    EXPECT_EQ(r.error->data["path"], "/binding");
    ASSERT_EQ(r.error->data["conflicts"].size(), 1u) << r.error->data.dump();
    EXPECT_EQ(r.error->data["conflicts"][0]["id"], "gui.quick_load");
    EXPECT_EQ(r.error->data["conflicts"][0]["label"], "File > Quick Load");
    EXPECT_EQ(bindings(w, "gui.quick_save"), Json::array({"Ctrl+S", ""})) << "nothing changed";

    call(w, "gui.set_binding", {{"id", "gui.quick_save"}, {"binding", "Ctrl+L"}, {"force", true}});
    EXPECT_EQ(bindings(w, "gui.quick_save"), Json::array({"Ctrl+L", ""}));
    EXPECT_EQ(bindings(w, "gui.quick_load"), Json::array({"", ""})) << "the other action lost it";

    // A hotkey on a game key, and a game button on another's key.
    r = w.session().call("gui.set_binding", {{"id", "gui.reset"}, {"binding", "X"}});
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(r.error->data["conflicts"][0]["id"], "p1.a");
    r = w.session().call("gui.set_binding", {{"id", "p2.a"}, {"binding", "Z"}});
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(r.error->data["conflicts"][0]["id"], "p1.b");
    r = w.session().call("gui.set_binding", {{"id", "p1.a"}, {"binding", "P"}});
    ASSERT_TRUE(r.isError()) << "a game button on Pause's key";
    EXPECT_EQ(r.error->data["conflicts"][0]["id"], "gui.toggle_pause");

    // The platform's own shortcut, which Exit keeps.
#ifdef Q_OS_MACOS
    const char* reserved = "Ctrl+Q"; // Cmd+Q
#else
    const char* reserved = "Alt+F4";
#endif
    r = w.session().call("gui.set_binding", {{"id", "gui.reset"}, {"binding", reserved}});
    ASSERT_TRUE(r.isError());
    bool system = false;
    for (const Json& c : r.error->data["conflicts"]) system = system || c["id"] == "system";
    EXPECT_TRUE(system) << r.error->data.dump();
}

TEST(GuiKeyBindings, BadArgumentsNameTheField)
{
    MainWindow w(windowOptions());
    const auto fails = [&](const Json& args, const char* path) {
        const Result r = w.session().call("gui.set_binding", args);
        ASSERT_TRUE(r.isError()) << args.dump();
        EXPECT_EQ(r.error->name, "InvalidArguments") << args.dump();
        EXPECT_EQ(r.error->data["path"], path) << args.dump();
    };
    fails({{"id", "gui.no_such"}, {"binding", "K"}}, "/id");
    fails({{"id", "gui.open_recent"}, {"binding", "K"}}, "/id");
    fails({{"id", "gui.quick_save"}, {"binding", "Ctrl+"}}, "/binding");
    fails({{"id", "gui.quick_save"}, {"binding", "K, L"}}, "/binding");
    fails({{"id", "p1.a"}, {"binding", "Ctrl+K"}}, "/binding");
    fails({{"id", "p1.a"}, {"binding", "Pad:Back+South"}}, "/binding"); // a game button takes one control
    fails({{"id", "gui.quick_save"}, {"binding", "Pad:Nope"}}, "/binding");
    fails({{"id", "gui.quick_save"}, {"binding", "Pad:Back+South+North"}}, "/binding");
    fails({{"id", "gui.quick_save"}, {"binding", "K"}, {"index", 2}}, "/index");
}

// FR-032 remapped: the game follows the new key, and the old one is free.
TEST(GuiKeyBindings, ARemappedGameButtonDrivesTheController)
{
    MainWindow w(windowOptions());
    w.show();
    w.activateWindow();
    ASSERT_TRUE(waitUntil([&] { return QApplication::activeWindow() == &w; }));
    openGame(w);
    call(w, "gui.set_binding", {{"id", "p1.a"}, {"binding", "K"}});
    call(w, "gui.set_binding", {{"id", "p2.start"}, {"binding", "M"}});
    QApplication::processEvents();

    key(w.view(), QEvent::KeyPress, Qt::Key_K);
    EXPECT_TRUE(waitUntil([&] { return call(w, "core.get_controller", {{"port", 1}})["buttons"] == Json::array({"A"}); }));
    key(w.view(), QEvent::KeyRelease, Qt::Key_K);
    key(w.view(), QEvent::KeyPress, Qt::Key_X);
    QApplication::processEvents();
    EXPECT_TRUE(waitUntil([&] { return call(w, "core.get_controller", {{"port", 1}})["buttons"] == Json::array(); }))
        << "X no longer presses A";
    key(w.view(), QEvent::KeyRelease, Qt::Key_X);
    key(w.view(), QEvent::KeyPress, Qt::Key_M);
    EXPECT_TRUE(waitUntil([&] { return call(w, "core.get_controller", {{"port", 2}})["buttons"] == Json::array({"Start"}); }));
    key(w.view(), QEvent::KeyRelease, Qt::Key_M);
}

// The page: reached from the browser, Help and Settings; a binding is
// captured by pressing it, Esc cancels a capture, and a conflict asks first.
TEST(GuiKeyBindings, ThePageRebindsByKeyPressAndReturns)
{
    MainWindow w(windowOptions());
    w.show();
    w.activateWindow();
    ASSERT_TRUE(waitUntil([&] { return QApplication::activeWindow() == &w; }));
    ASSERT_TRUE(waitUntil([&] { return w.isShowingLibrary(); }));

    auto* button = w.findChild<QAbstractButton*>(QStringLiteral("keyBindingsButton"));
    ASSERT_TRUE(button) << "the library browser has a Key Bindings button";
    button->click();
    ASSERT_TRUE(waitUntil([&] { return page(w) && page(w)->isVisible(); }));
    auto* tree = page(w)->findChild<QTreeWidget*>(QStringLiteral("keyBindingsTree"));
    ASSERT_TRUE(tree);
    QTreeWidgetItem* quickSave = row(tree, QStringLiteral("gui.quick_save"));
    ASSERT_TRUE(quickSave);
    // For a look at the page without a screen: CUTENES_GUI_SNAPSHOTS=<dir>.
    if (const QString dir = qEnvironmentVariable("CUTENES_GUI_SNAPSHOTS"); !dir.isEmpty()) {
        w.resize(900, 700);
        QApplication::processEvents();
        w.grab().save(dir + QStringLiteral("/key-bindings-page.png"));
    }
    EXPECT_EQ(quickSave->text(1), QKeySequence(QStringLiteral("Ctrl+S")).toString(QKeySequence::NativeText));

    // Enter on a binding captures the next key combination.
    tree->setFocus();
    tree->setCurrentItem(quickSave, 1);
    key(tree, QEvent::KeyPress, Qt::Key_Return);
    ASSERT_TRUE(waitUntil([&] { return quickSave->text(1).contains(QStringLiteral("Press")); }));
    key(tree, QEvent::KeyPress, Qt::Key_Control, Qt::ControlModifier); // a modifier alone waits
    key(tree, QEvent::KeyPress, Qt::Key_J, Qt::ControlModifier);
    EXPECT_TRUE(waitUntil([&] { return bindings(w, "gui.quick_save") == Json::array({"Ctrl+J", ""}); }));
    EXPECT_TRUE(waitUntil([&] { return shortcuts(action(w, "gui.quick_save")) == QStringList{"Ctrl+J"}; }));

    // Esc cancels a capture and changes nothing; the next Esc goes back.
    tree->setCurrentItem(quickSave, 2);
    key(tree, QEvent::KeyPress, Qt::Key_Return);
    ASSERT_TRUE(waitUntil([&] { return quickSave->text(2).contains(QStringLiteral("Press")); }));
    key(tree, QEvent::KeyPress, Qt::Key_Escape);
    EXPECT_TRUE(waitUntil([&] { return !quickSave->text(2).contains(QStringLiteral("Press")); }));
    EXPECT_EQ(bindings(w, "gui.quick_save"), Json::array({"Ctrl+J", ""}));
    EXPECT_TRUE(page(w)->isVisible());

    // A conflict asks; Yes takes the key from the other action.
    tree->setCurrentItem(quickSave, 2);
    key(tree, QEvent::KeyPress, Qt::Key_Return);
    ASSERT_TRUE(waitUntil([&] { return quickSave->text(2).contains(QStringLiteral("Press")); }));
    key(tree, QEvent::KeyPress, Qt::Key_L, Qt::ControlModifier);
    QMessageBox* ask = nullptr;
    ASSERT_TRUE(waitUntil([&] { return (ask = w.findChild<QMessageBox*>(QStringLiteral("bindingConflict"))) != nullptr; }));
    EXPECT_TRUE(ask->text().contains(QStringLiteral("Quick Load"))) << ask->text().toStdString();
    EXPECT_EQ(bindings(w, "gui.quick_save"), Json::array({"Ctrl+J", ""})) << "nothing changes before the answer";
    ask->button(QMessageBox::Yes)->click();
    EXPECT_TRUE(waitUntil([&] { return bindings(w, "gui.quick_save") == Json::array({"Ctrl+J", "Ctrl+L"}); }))
        << bindings(w, "gui.quick_save").dump();
    EXPECT_EQ(bindings(w, "gui.quick_load"), Json::array({"", ""}));

    // The filter keeps the matching rows.
    auto* filter = page(w)->findChild<QLineEdit*>(QStringLiteral("keyBindingsFilter"));
    ASSERT_TRUE(filter);
    filter->setText(QStringLiteral("quick"));
    EXPECT_FALSE(quickSave->isHidden());
    EXPECT_TRUE(row(tree, QStringLiteral("gui.reset"))->isHidden());
    EXPECT_TRUE(row(tree, QStringLiteral("p1.a"))->parent()->isHidden()) << "a group with no match hides";
    filter->clear();
    EXPECT_FALSE(row(tree, QStringLiteral("gui.reset"))->isHidden());

    // Reset All on the page.
    page(w)->findChild<QPushButton*>(QStringLiteral("keyBindingsResetAll"))->click();
    EXPECT_TRUE(waitUntil([&] { return bindings(w, "gui.quick_save") == Json::array({"Ctrl+S", ""}); }));

    key(tree, QEvent::KeyPress, Qt::Key_Escape);
    EXPECT_TRUE(waitUntil([&] { return w.isShowingLibrary(); })) << "Esc returns to the screen it came from";
}

TEST(GuiKeyBindings, HelpAndSettingsOpenThePageAndAGameResumesOnBack)
{
    MainWindow w(windowOptions());
    w.show();
    openGame(w);
    ASSERT_TRUE(action(w, "gui.show_shortcuts")->isEnabled());
    action(w, "gui.show_shortcuts")->trigger();
    ASSERT_TRUE(waitUntil([&] { return page(w) && page(w)->isVisible(); }));
    EXPECT_EQ(call(w, "core.run_state")["runState"], "Paused") << "the page pauses the game";
    page(w)->findChild<QPushButton*>(QStringLiteral("keyBindingsBack"))->click();
    ASSERT_TRUE(waitUntil([&] { return w.view()->isVisible(); }));
    EXPECT_TRUE(waitUntil([&] { return call(w, "core.run_state")["runState"] == "Running"; }))
        << "Back resumes the game it paused";

    // Settings > Hotkeys is the page at the menu actions; its controls
    // section opens with gui.show_shortcuts (Settings > Input is a page of
    // its own, with a Key Bindings button).
    for (const char* section : {"hotkeys", "controls"}) {
        if (QLatin1String(section) == QLatin1String("hotkeys")) {
            EXPECT_EQ(call(w, "gui.show_settings", {{"page", "hotkeys"}})["page"], "hotkeys");
        } else {
            call(w, "gui.show_shortcuts", {{"section", "controls"}});
        }
        ASSERT_TRUE(waitUntil([&] { return page(w) && page(w)->isVisible(); })) << section;
        auto* tree = page(w)->findChild<QTreeWidget*>(QStringLiteral("keyBindingsTree"));
        const QString id = tree->currentItem() ? tree->currentItem()->data(0, Qt::UserRole).toString() : QString();
        EXPECT_EQ(id.startsWith(QStringLiteral("p1.")), QLatin1String(section) == QLatin1String("controls"))
            << section << ": the page opens at its section, at " << id.toStdString();
        call(w, "gui.show_library");
        ASSERT_TRUE(waitUntil([&] { return w.isShowingLibrary(); }));
    }
}
