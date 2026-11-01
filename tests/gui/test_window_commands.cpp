// G090 (contract "Window commands" and "Queries"): the commands that report
// and close the windows, found missing by the gui-audit test.
// - gui.list_windows names the windows a gui.show_* opened that are shown,
//   and gui.close_window closes one, or all; a window not open is refused.
// - gui.state reports what the window shows and how the game plays, each
//   value the one the command that set it gave.

#include "gui_support.hpp"
#include "mainwindow.hpp"

#include <QDialog>

#include <gtest/gtest.h>

using cutenes::api::Json;
using cutenes::api::Result;
using cutenes::app::MainWindow;
using cutenes::test::waitUntil;
using cutenes::test::windowOptions;

namespace {

const QString kNestest = QStringLiteral(CUTENES_TEST_ROMS "/other/nestest.nes");

Json call(MainWindow& w, const char* name, const Json& args = Json::object())
{
    const Result r = w.session().call(name, args);
    EXPECT_FALSE(r.isError()) << name << ": " << (r.isError() ? r.error->message : "");
    return r.value;
}

bool windowsAre(MainWindow& w, const Json& names)
{
    return call(w, "gui.list_windows")["windows"] == names;
}

void openNestest(MainWindow& w)
{
    w.show();
    call(w, "gui.set_setting", {{"key", "emulation.pauseWhenInactive"}, {"value", false}});
    call(w, "gui.open_rom", {{"path", kNestest.toStdString()}});
}

} // namespace

TEST(GuiWindowCommands, ListWindowsNamesTheOpenWindowsAndCloseWindowClosesThem)
{
    MainWindow w(windowOptions());
    openNestest(w);
    EXPECT_TRUE(windowsAre(w, Json::array()));
    call(w, "gui.show_memory_viewer");
    call(w, "gui.show_debugger");
    call(w, "gui.show_about");
    ASSERT_TRUE(waitUntil([&] { return windowsAre(w, {"about", "debugger", "memory_viewer"}); }))
        << call(w, "gui.list_windows").dump();

    EXPECT_EQ(call(w, "gui.close_window", {{"window", "debugger"}})["closed"], Json::array({"debugger"}));
    ASSERT_TRUE(waitUntil([&] { return windowsAre(w, {"about", "memory_viewer"}); }));
    EXPECT_FALSE(w.findChild<QDialog*>(QStringLiteral("debuggerWindow"))->isVisible());
    // Closed already: refused.
    const Result again = w.session().call("gui.close_window", {{"window", "debugger"}});
    ASSERT_TRUE(again.isError());
    EXPECT_EQ(again.error->name, "InvalidArguments");

    EXPECT_EQ(call(w, "gui.close_window", {{"window", "all"}})["closed"], Json::array({"about", "memory_viewer"}));
    ASSERT_TRUE(waitUntil([&] { return windowsAre(w, Json::array()); }));
    EXPECT_FALSE(w.findChild<QDialog*>(QStringLiteral("memoryViewer"))->isVisible());
    EXPECT_EQ(call(w, "gui.close_window", {{"window", "all"}})["closed"], Json::array());
}

TEST(GuiWindowCommands, StateReportsThePageThePlayAndTheWindows)
{
    MainWindow w(windowOptions());
    w.show();
    ASSERT_TRUE(waitUntil([&] { return call(w, "gui.state")["page"] == "library"; }));
    Json state = call(w, "gui.state");
    EXPECT_EQ(state["game"], false);
    EXPECT_EQ(state["movie"]["mode"], "none");

    openNestest(w);
    call(w, "gui.set_scale", {{"scale", 3}});
    call(w, "gui.set_speed", {{"percent", 50}});
    call(w, "gui.select_slot", {{"slot", 4}});
    call(w, "gui.toggle_fast_forward");
    call(w, "gui.show_cheats");
    ASSERT_TRUE(waitUntil([&] { return call(w, "gui.state")["page"] == "game" && !call(w, "gui.state")["windows"].empty(); }));
    state = call(w, "gui.state");
    EXPECT_EQ(state["game"], true);
    EXPECT_EQ(state["scale"], 3);
    EXPECT_EQ(state["speed"], 50);
    EXPECT_EQ(state["selectedSlot"], 4);
    EXPECT_EQ(state["fastForward"], true);
    EXPECT_EQ(state["rewinding"], false);
    EXPECT_EQ(state["fullscreen"], false);
    EXPECT_EQ(state["windows"], Json::array({"cheats"}));

    call(w, "gui.set_speed", {{"percent", "unlimited"}});
    call(w, "gui.toggle_fast_forward");
    state = call(w, "gui.state");
    EXPECT_EQ(state["speed"], 0) << "Unlimited";
    EXPECT_EQ(state["fastForward"], false);

    call(w, "gui.show_shortcuts");
    EXPECT_TRUE(waitUntil([&] { return call(w, "gui.state")["page"] == "key_bindings"; }));
}
