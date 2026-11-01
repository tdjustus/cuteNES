// G032 (A6): rewind, fast-forward, speed and Frame Advance (spec US4-4 to
// US4-6, FR-034, FR-035, research R4 and R6, the menus contract). The core
// is deterministic, so a game rewound to frame N must be in exactly the
// state a fresh run of N frames reaches.

#include "gui_support.hpp"
#include "mainwindow.hpp"

#include <cutenes/qt/audio_output.hpp>
#include <cutenes/qt/emulation_view.hpp>

#include <QAction>
#include <QLabel>
#include <QMenuBar>

#include <gtest/gtest.h>

#include <cmath>

using cutenes::api::Json;
using cutenes::api::Result;
using cutenes::app::MainWindow;
using cutenes::test::waitUntil;
using cutenes::test::windowOptions;

namespace {

constexpr const char* kRelative = "instr_test-v5/rom_singles/01-basics.nes";
const QString kRom = QStringLiteral(CUTENES_TEST_ROMS "/") + QLatin1String(kRelative);
constexpr double kNtsc = 60.0988;

Json call(MainWindow& w, const char* name, const Json& args = Json::object())
{
    const Result r = w.session().call(name, args);
    EXPECT_FALSE(r.isError()) << name << ": " << (r.isError() ? r.error->message : "");
    return r.value;
}

std::uint64_t frame(MainWindow& w)
{
    return call(w, "core.counters")["frame"].get<std::uint64_t>();
}

QByteArray stateOf(cutenes::api::Session& s)
{
    const Result captured = s.call("core.capture_state");
    EXPECT_FALSE(captured.isError());
    QByteArray bytes;
    for (bool eof = false; !eof;) {
        const Result part = s.call("session.blob_read",
            {{"blobId", captured.value["blobId"]}, {"offset", bytes.size()}, {"length", 65536}});
        bytes += QByteArray::fromBase64(QByteArray::fromStdString(part.value["bytes"].get<std::string>()));
        eof = part.value["eof"].get<bool>();
    }
    return bytes;
}

// A window playing 01-basics in real time.
void play(MainWindow& w)
{
    w.show();
    call(w, "gui.open_rom", {{"path", kRom.toStdString()}});
}

QAction* action(MainWindow& w, const char* command, const QVariantMap& args = {})
{
    for (QAction* a : w.menuBar()->findChildren<QAction*>()) {
        if (a->property("command").toString() != QLatin1String(command)) continue;
        if (args.isEmpty() || a->property("commandArgs").toMap() == args) return a;
    }
    return nullptr;
}

} // namespace

TEST(GuiRewind, RewindingLandsOnTheStateAFreshRunReaches)
{
    MainWindow w(windowOptions());
    play(w);
    ASSERT_TRUE(waitUntil([&] { return frame(w) >= 300; }, 15000));
    EXPECT_FALSE(call(w, "gui.set_rewind", {{"on", true}})["rewinding"] != true);
    const std::uint64_t from = frame(w);
    ASSERT_TRUE(waitUntil([&] { return frame(w) + 120 <= from; }, 15000)) << "play runs backwards";
    call(w, "gui.set_rewind", {{"on", false}});
    call(w, "core.pause");
    const std::uint64_t at = frame(w);
    EXPECT_LE(at + 100, from);
    const QByteArray rewound = stateOf(w.session());

    cutenes::api::SessionConfig config;
    config.libraryRoot = CUTENES_TEST_ROMS;
    cutenes::api::Session fresh(config);
    ASSERT_FALSE(fresh.call("core.load_rom", {{"path", kRelative}}).isError());
    ASSERT_FALSE(fresh.call("core.run_frames", {{"frames", at}}).isError());
    EXPECT_EQ(rewound, stateOf(fresh)) << "the state of frame " << at;
}

TEST(GuiRewind, ReleasingRewindPlaysOnFromThere)
{
    MainWindow w(windowOptions());
    play(w);
    ASSERT_TRUE(waitUntil([&] { return frame(w) >= 120; }, 15000));
    call(w, "gui.set_rewind", {{"on", true}});
    const std::uint64_t from = frame(w);
    ASSERT_TRUE(waitUntil([&] { return frame(w) + 30 <= from; }, 15000));
    call(w, "gui.set_rewind", {{"on", false}});
    const std::uint64_t released = frame(w);
    EXPECT_TRUE(waitUntil([&] { return frame(w) > released + 30; })) << "forwards again";
    // The history before the release is still there.
    call(w, "gui.set_rewind", {{"on", true}});
    const std::uint64_t again = frame(w);
    EXPECT_TRUE(waitUntil([&] { return frame(w) + 40 <= again; }, 15000));
    call(w, "gui.set_rewind", {{"on", false}});
}

TEST(GuiRewind, RewindFromAPausedGamePausesAgainAfter)
{
    MainWindow w(windowOptions());
    play(w);
    ASSERT_TRUE(waitUntil([&] { return frame(w) >= 90; }, 15000));
    call(w, "core.pause");
    const std::uint64_t from = frame(w);
    call(w, "gui.set_rewind", {{"on", true}});
    ASSERT_TRUE(waitUntil([&] { return frame(w) + 20 <= from; }, 15000));
    call(w, "gui.set_rewind", {{"on", false}});
    EXPECT_EQ(call(w, "core.run_state")["runState"], "Paused");
}

TEST(GuiRewind, ANewTimelineHasNothingToRewind)
{
    MainWindow w(windowOptions());
    play(w);
    ASSERT_TRUE(waitUntil([&] { return frame(w) >= 30; }, 15000));
    call(w, "gui.save_state", {{"slot", 1}});
    call(w, "gui.load_state", {{"slot", 1}}); // a load starts a new timeline
    EXPECT_EQ(call(w, "gui.set_rewind", {{"on", true}})["rewinding"], false);
    call(w, "gui.reset");
    EXPECT_EQ(call(w, "gui.set_rewind", {{"on", true}})["rewinding"], false);
}

TEST(GuiRewind, RewindMutesTheSoundAndSaysRewind)
{
    MainWindow w(windowOptions());
    play(w);
    ASSERT_TRUE(waitUntil([&] { return frame(w) >= 60; }, 15000));
    call(w, "gui.set_rewind", {{"on", true}});
    EXPECT_TRUE(w.audio()->muted());
    auto* status = w.findChild<QLabel*>(QStringLiteral("speedOverlay"));
    ASSERT_NE(status, nullptr);
    EXPECT_TRUE(waitUntil([&] { return status->isVisible() && status->text() == QStringLiteral("◀◀ REWIND"); }));
    call(w, "gui.set_rewind", {{"on", false}});
    EXPECT_FALSE(w.audio()->muted());
    EXPECT_TRUE(waitUntil([&] { return !status->isVisible(); }));
}

// Holding fast-forward with rewind rewinds at the fast-forward speed, and
// says so.
TEST(GuiRewind, RewindWithFastForwardShowsFastRewind)
{
    MainWindow w(windowOptions());
    play(w);
    ASSERT_TRUE(waitUntil([&] { return frame(w) >= 120; }, 15000));
    call(w, "gui.set_rewind", {{"on", true}});
    call(w, "gui.set_fast_forward", {{"on", true}});
    EXPECT_NEAR(w.frameRate(), 3 * kNtsc, 0.05);
    auto* status = w.findChild<QLabel*>(QStringLiteral("speedOverlay"));
    EXPECT_TRUE(waitUntil([&] { return status->isVisible() && status->text() == QStringLiteral("◀◀ FAST REWIND"); }));
    call(w, "gui.set_fast_forward", {{"on", false}});
    EXPECT_TRUE(waitUntil([&] { return status->text() == QStringLiteral("◀◀ REWIND"); }));
    call(w, "gui.set_rewind", {{"on", false}});
}

// The speed indicators look like PAUSED, in its size and style, but sit in
// the top right of the picture so they keep out of the way of play.
TEST(GuiSpeed, SpeedIndicatorsLookLikePausedInTheTopRight)
{
    MainWindow w(windowOptions());
    play(w);
    ASSERT_TRUE(waitUntil([&] { return frame(w) > 0; }));
    auto* speed = w.findChild<QLabel*>(QStringLiteral("speedOverlay"));
    auto* paused = w.findChild<QLabel*>(QStringLiteral("pausedOverlay"));
    ASSERT_NE(speed, nullptr);
    ASSERT_NE(paused, nullptr);
    EXPECT_EQ(speed->parentWidget(), w.view());
    EXPECT_EQ(speed->styleSheet(), paused->styleSheet()) << "one design";
    auto shows = [&](const QString& text) {
        return waitUntil([&] {
            const QRect r = speed->geometry();
            const QRect view = w.view()->rect();
            return speed->isVisible() && speed->text() == text && r.top() > view.top()
                && r.top() < view.height() / 4 && r.right() < view.right()
                && r.right() > view.right() - view.width() / 10;
        });
    };
    call(w, "gui.set_speed", {{"percent", 50}});
    EXPECT_TRUE(shows(QStringLiteral("SPEED 50%")));
    call(w, "gui.set_fast_forward", {{"on", true}});
    EXPECT_TRUE(shows(QStringLiteral("▶▶ FAST FORWARD")));
    call(w, "gui.set_fast_forward", {{"on", false}});
    call(w, "gui.set_speed", {{"percent", "unlimited"}});
    EXPECT_TRUE(shows(QStringLiteral("▶▶ UNLIMITED")));
    call(w, "core.pause");
    EXPECT_TRUE(waitUntil([&] { return paused->isVisible(); }));
    EXPECT_EQ(paused->geometry().center(), w.view()->rect().center()) << "PAUSED stays in the middle";
    call(w, "core.resume");
    call(w, "gui.set_speed", {{"percent", 100}});
    EXPECT_TRUE(waitUntil([&] { return !speed->isVisible(); })) << "nothing at normal speed";
}

TEST(GuiSpeed, FastForwardRunsAtThreeTimesAndMutesAboveTwo)
{
    MainWindow w(windowOptions());
    play(w);
    ASSERT_TRUE(waitUntil([&] { return frame(w) > 0; }));
    EXPECT_NEAR(w.frameRate(), kNtsc, 0.01);
    EXPECT_EQ(call(w, "gui.set_fast_forward", {{"on", true}})["fastForward"], true);
    EXPECT_NEAR(w.frameRate(), 3 * kNtsc, 0.05) << "data-model fastForwardSpeed 300";
    EXPECT_TRUE(w.audio()->muted()) << "R4: muted above 2x";
    auto* status = w.findChild<QLabel*>(QStringLiteral("speedOverlay"));
    EXPECT_TRUE(waitUntil([&] { return status->isVisible() && status->text() == QStringLiteral("▶▶ FAST FORWARD"); }));
    call(w, "gui.set_fast_forward", {{"on", false}});
    EXPECT_NEAR(w.frameRate(), kNtsc, 0.01);
    EXPECT_FALSE(w.audio()->muted());

    EXPECT_EQ(call(w, "gui.toggle_fast_forward")["fastForward"], true);
    EXPECT_EQ(call(w, "gui.toggle_fast_forward")["fastForward"], false);
}

TEST(GuiSpeed, SpeedScalesTheClockAndTheSound)
{
    MainWindow w(windowOptions());
    play(w);
    EXPECT_EQ(call(w, "gui.set_speed", {{"percent", 50}})["speed"], 50);
    EXPECT_NEAR(w.frameRate(), kNtsc / 2, 0.01);
    EXPECT_DOUBLE_EQ(w.audio()->speed(), 0.5) << "slowed with the game";
    EXPECT_FALSE(w.audio()->muted());
    call(w, "gui.set_speed", {{"percent", 200}});
    EXPECT_DOUBLE_EQ(w.audio()->speed(), 2.0);
    EXPECT_FALSE(w.audio()->muted()) << "sped up to 2x";
    call(w, "gui.set_speed", {{"percent", "unlimited"}});
    EXPECT_GT(w.frameRate(), 10 * kNtsc);
    EXPECT_TRUE(w.audio()->muted());

    EXPECT_EQ(call(w, "gui.set_speed", {{"step", "slower"}})["speed"], 400);
    EXPECT_EQ(call(w, "gui.set_speed", {{"step", "faster"}})["speed"], "unlimited");
    call(w, "gui.set_speed", {{"percent", 25}});
    EXPECT_EQ(call(w, "gui.set_speed", {{"step", "slower"}})["speed"], 25) << "the slowest stays";
    EXPECT_EQ(call(w, "gui.set_speed", {{"step", "faster"}})["speed"], 50);

    for (const Json& bad : {Json(125), Json("fast")}) {
        const Result r = w.session().call("gui.set_speed", {{"percent", bad}});
        ASSERT_TRUE(r.isError()) << bad.dump();
        EXPECT_EQ(r.error->name, "InvalidArguments");
    }
}

TEST(GuiSpeed, FrameAdvanceRunsExactlyOneFrame)
{
    MainWindow w(windowOptions());
    play(w);
    ASSERT_TRUE(waitUntil([&] { return frame(w) >= 10; }));
    call(w, "gui.frame_advance"); // a running game pauses first
    EXPECT_EQ(call(w, "core.run_state")["runState"], "Paused");
    const std::uint64_t at = frame(w);
    call(w, "gui.frame_advance");
    EXPECT_EQ(frame(w), at + 1);
    call(w, "gui.frame_advance");
    EXPECT_EQ(frame(w), at + 2);
    // And a frame advanced through can be rewound.
    call(w, "gui.set_rewind", {{"on", true}});
    EXPECT_TRUE(waitUntil([&] { return frame(w) < at; }));
    call(w, "gui.set_rewind", {{"on", false}});
}

// From the menu nothing is held, so the hold items toggle.
TEST(GuiSpeed, TheHoldItemsToggleFromTheMenu)
{
    MainWindow w(windowOptions());
    play(w);
    QAction* fastForward = action(w, "gui.set_fast_forward");
    ASSERT_TRUE(waitUntil([&] { return fastForward->isEnabled(); }));
    fastForward->trigger();
    EXPECT_TRUE(waitUntil([&] { return w.playback().fastForward; }));
    fastForward->trigger();
    EXPECT_TRUE(waitUntil([&] { return !w.playback().fastForward; }));
    EXPECT_FALSE(fastForward->autoRepeat()) << "a held key presses it once";
}

TEST(GuiSpeed, TheSpeedMenuRunsItsCommand)
{
    MainWindow w(windowOptions());
    play(w);
    QAction* half = action(w, "gui.set_speed", {{QStringLiteral("percent"), 50}});
    ASSERT_NE(half, nullptr);
    ASSERT_TRUE(half->isEnabled()) << "speed needs no game";
    half->trigger();
    EXPECT_TRUE(waitUntil([&] { return std::abs(w.frameRate() - kNtsc / 2) < 0.01; }));
}
