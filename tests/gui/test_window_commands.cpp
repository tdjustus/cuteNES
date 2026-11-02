// G090 (contract "Window commands" and "Queries"): the commands that report
// and close the windows, found missing by the gui-audit test.
// - gui.list_windows names the windows a gui.show_* opened that are shown,
//   and gui.close_window closes one, or all; a window not open is refused.
// - gui.state reports what the window shows and how the game plays, each
//   value the one the command that set it gave.

#include "gui_support.hpp"
#include "mainwindow.hpp"

#include <QCoreApplication>
#include <QDialog>

#include <gtest/gtest.h>

#include <chrono>
#include <cmath>

#ifdef _WIN32
#include <windows.h>
#endif

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

// #117 (contract "Settings": "gui.get_settings (optional group)"; its
// description: "Every setting's value and schema, or one group's"): with a
// group, the values hold that group alone and the schema only its keys
// (<group>.<key>, or the group's own name for one-key groups); without one,
// every group.
TEST(GuiWindowCommands, GetSettingsWithAGroupReturnsThatGroupOnly)
{
    MainWindow w(windowOptions());
    const std::vector<std::string> groups = {
        "video", "audio", "emulation", "library", "servers", "hotkeys", "input", "updates"};
    const Json all = call(w, "gui.get_settings");
    for (const std::string& group : groups) {
        EXPECT_TRUE(all["values"].contains(group)) << group;
    }
    for (const std::string& group : groups) {
        const Json one = call(w, "gui.get_settings", {{"group", group}});
        ASSERT_TRUE(one["values"].is_object()) << group;
        EXPECT_EQ(one["values"].size(), 1u) << group << ": " << one["values"].dump();
        ASSERT_TRUE(one["values"].contains(group)) << group;
        EXPECT_EQ(one["values"][group].is_object() || one["values"][group].is_array(),
            all["values"][group].is_object() || all["values"][group].is_array())
            << group;
        size_t expected = 0;
        for (const auto& [key, _] : all["schema"].items()) {
            if (key == group || key.rfind(group + ".", 0) == 0) ++expected;
        }
        EXPECT_GT(expected, 0u) << group;
        EXPECT_EQ(one["schema"].size(), expected) << group << ": " << one["schema"].dump();
        for (const auto& [key, _] : one["schema"].items()) {
            EXPECT_TRUE(key == group || key.rfind(group + ".", 0) == 0) << group << " has " << key;
        }
    }
    if (!HasFailure()) {
        // The groups together make up the whole.
        Json values = Json::object();
        for (const std::string& group : groups) {
            values[group] = call(w, "gui.get_settings", {{"group", group}})["values"][group];
        }
        values["video"].erase("pacingNow"); // measured, not stored
        Json whole = all["values"];
        whole["video"].erase("pacingNow");
        EXPECT_EQ(values, whole);
    }
}

// #124: macOS's App Nap slowed a running game to a few frames a second while
// its window was not visible (display asleep, an MCP client driving it). The
// window holds a "user-initiated, latency-critical" activity while a game is
// running, and only then: pausing or closing the game ends it, so a paused
// or idle cuteNES still naps. Elsewhere the activity is only bookkeeping.
// #151: display pacing runs from the frame clock's own thread at the
// display's rate; it never waits for the window system to say a frame was
// shown. A hidden window gets no frame callbacks (Wayland withholds them,
// and offscreen has none), so a game whose window was shown on a display
// and then hidden must still run. The floor is far below 60 fps because a
// loaded CI runner is slow; what it catches is a stall.
TEST(GuiWindowCommands, DisplayPacingKeepsAHiddenWindowsGameRunning)
{
    MainWindow w(windowOptions());
    call(w, "gui.set_setting", {{"key", "emulation.pauseWhenInactive"}, {"value", false}});
    call(w, "gui.set_setting", {{"key", "video.pacing"}, {"value", "display"}});
    w.show();
    const auto now = [&] { return call(w, "gui.get_settings", {{"group", "video"}})["values"]["video"]["pacingNow"]; };
    ASSERT_TRUE(waitUntil([&] { return now()["displayHz"].get<double>() > 0; })) << now().dump();
    const double hz = now()["displayHz"].get<double>();
    if (std::abs(hz - 60.0988) > 0.600988 && std::abs(hz / 2 - 60.0988) > 0.600988) {
        GTEST_SKIP() << "a " << hz << " Hz display cannot pace the game, so display pacing is not in use";
    }
    w.hide();
    openNestest(w);
    EXPECT_EQ(now()["mode"], "display") << now().dump();
    ASSERT_TRUE(waitUntil([&] { return call(w, "core.run_state")["runState"] == "Running"; }));
    const auto frame = [&] { return call(w, "core.run_state")["frame"].get<std::uint64_t>(); };
    const std::uint64_t start = frame();
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < until) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    EXPECT_GE(frame() - start, 30u) << "a hidden window's game stalled";
}

// #151: on Windows a running game opts out of power throttling: execution
// speed (EcoQoS) and the timer-resolution exemption Windows 11 applies to a
// process whose windows are hidden (FrameClock relies on timeBeginPeriod).
// Pausing hands the decision back to Windows. Read back from the OS.
#ifdef _WIN32
TEST(GuiWindowCommands, OnWindowsARunningGameIsNotThrottled)
{
    const auto state = [] {
        PROCESS_POWER_THROTTLING_STATE s{};
        s.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
        EXPECT_TRUE(GetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &s, sizeof s))
            << "GetProcessInformation: " << GetLastError();
        return s;
    };
    constexpr ULONG kBoth = PROCESS_POWER_THROTTLING_EXECUTION_SPEED | PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION;
    MainWindow w(windowOptions());
    w.show();
    openNestest(w);
    ASSERT_TRUE(waitUntil([&] { return w.holdsRunningActivity(); }));
    PROCESS_POWER_THROTTLING_STATE s = state();
    EXPECT_EQ(s.ControlMask & kBoth, kBoth) << "Windows decides, not cuteNES";
    EXPECT_EQ(s.StateMask & kBoth, 0u) << "throttling is on";
    call(w, "gui.toggle_pause");
    ASSERT_TRUE(waitUntil([&] { return !w.holdsRunningActivity(); }));
    s = state();
    EXPECT_EQ(s.ControlMask & kBoth, 0u) << "paused: Windows decides again";
}
#endif

TEST(GuiWindowCommands, AnActivityIsHeldExactlyWhileAGameRuns)
{
    MainWindow w(windowOptions());
    w.show();
    EXPECT_FALSE(w.holdsRunningActivity()) << "no game";
    openNestest(w);
    ASSERT_TRUE(waitUntil([&] { return call(w, "core.run_state")["runState"] == "Running"; }));
    EXPECT_TRUE(waitUntil([&] { return w.holdsRunningActivity(); })) << "running";
    call(w, "gui.toggle_pause");
    EXPECT_TRUE(waitUntil([&] { return !w.holdsRunningActivity(); })) << "paused";
    call(w, "gui.toggle_pause");
    EXPECT_TRUE(waitUntil([&] { return w.holdsRunningActivity(); })) << "resumed";
    call(w, "gui.close_game");
    EXPECT_TRUE(waitUntil([&] { return !w.holdsRunningActivity(); })) << "closed";
}
