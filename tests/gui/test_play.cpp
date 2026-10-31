// G016-G018: playing a game in the window. Expectations come from the spec
// (US1, FR-021 game-only actions, FR-031 input), the menus contract (each
// action runs its gui.* command), contracts/cli.md, and the test ROM's own
// documentation: blargg's instr_test-v5 writes $80 to $6000 while running
// and 0 when every test passed, with the signature DE B0 61 at $6001.

#include "cli.hpp"
#include "gui_support.hpp"
#include "mainwindow.hpp"

#include <cutenes/qt/emulation_view.hpp>

#include <QAction>
#include <QApplication>
#include <QByteArray>
#include <QFile>
#include <QKeyEvent>
#include <QMenuBar>
#include <QMessageBox>
#include <QTemporaryDir>

#include <gtest/gtest.h>

using cutenes::api::Json;
using cutenes::api::Result;
using cutenes::app::MainWindow;
using cutenes::test::waitUntil;

namespace {

const QString kRom = QStringLiteral(CUTENES_TEST_ROMS "/instr_test-v5/rom_singles/01-basics.nes");

Json call(MainWindow& w, const char* name, const Json& args = Json::object())
{
    const Result r = w.session().call(name, args);
    EXPECT_FALSE(r.isError()) << name << ": " << (r.isError() ? r.error->message : "");
    return r.value;
}

std::string runState(MainWindow& w)
{
    return call(w, "core.run_state")["runState"].get<std::string>();
}

QAction* action(MainWindow& w, const char* command)
{
    for (QAction* a : w.menuBar()->findChildren<QAction*>()) {
        if (a->property("command").toString() == QLatin1String(command)) return a;
    }
    return nullptr;
}

// A window playing 01-basics, opened the way the user does.
void openGame(MainWindow& w)
{
    const Json r = call(w, "gui.open_rom", {{"path", kRom.toStdString()}});
    ASSERT_TRUE(r.contains("cartridge"));
    EXPECT_EQ(r["runState"], "Running");
}

int statusByte(MainWindow& w, int addr)
{
    const Json r = call(w, "core.peek", {{"addr", addr}, {"length", 1}});
    const QByteArray bytes = QByteArray::fromBase64(QByteArray::fromStdString(r["bytes"].get<std::string>()));
    return bytes.isEmpty() ? -1 : static_cast<unsigned char>(bytes[0]);
}

} // namespace

TEST(GuiPlay, WithoutAGameOnlyOpenExitAndAboutAreEnabled)
{
    MainWindow w;
    for (QAction* a : w.menuBar()->findChildren<QAction*>()) {
        const QString command = a->property("command").toString();
        if (command.isEmpty()) continue;
        const bool enabled = command == QLatin1String("gui.open_rom")
            || command == QLatin1String("gui.exit") || command == QLatin1String("gui.show_about");
        EXPECT_EQ(a->isEnabled(), enabled) << command.toStdString();
    }
}

TEST(GuiPlay, GameOnlyCommandsFailWithoutAGame)
{
    MainWindow w;
    for (const char* name : {"gui.close_game", "gui.toggle_pause", "gui.reset", "gui.power_cycle"}) {
        const Result r = w.session().call(name);
        ASSERT_TRUE(r.isError()) << name;
        EXPECT_EQ(r.error->name, "NoCartridge") << name;
    }
}

// The real-time loop plays the ROM to its end: the test passes on its own.
TEST(GuiPlay, OpeningARomPlaysItInRealTime)
{
    MainWindow w;
    openGame(w);
    ASSERT_TRUE(waitUntil([&] { return action(w, "gui.reset")->isEnabled(); }));
    EXPECT_TRUE(w.windowTitle().contains(QStringLiteral("01-basics.nes")));
    ASSERT_TRUE(waitUntil([&] { return statusByte(w, 0x6001) == 0xDE && statusByte(w, 0x6000) != 0x80; },
        20000)) << "the ROM did not finish in real time";
    EXPECT_EQ(statusByte(w, 0x6000), 0) << "01-basics reported a failure";
    EXPECT_EQ(statusByte(w, 0x6002), 0xB0);
    EXPECT_EQ(statusByte(w, 0x6003), 0x61);
}

// The view shows exactly the frame the core produced (contract
// media-output: 256x240 XRGB8888).
TEST(GuiPlay, TheViewShowsTheCoresFrame)
{
    MainWindow w;
    openGame(w);
    ASSERT_TRUE(waitUntil([&] { return call(w, "session.status")["frame"].get<std::uint64_t>() >= 30; }));
    call(w, "core.pause");
    w.view()->takeFrame(); // drain to the last frame produced

    const Json screen = call(w, "core.screen", {{"format", "raw"}});
    const Json blob = call(w, "session.blob_read", {{"blobId", screen["blobId"]}, {"offset", 0}, {"length", 61440}});
    const QByteArray raw = QByteArray::fromBase64(QByteArray::fromStdString(blob["bytes"].get<std::string>()));
    ASSERT_EQ(raw.size(), 61440);
    const auto& shown = w.view()->shownPixels();
    ASSERT_EQ(shown.size(), 256u * 240u);
    EXPECT_EQ(std::memcmp(shown.data(), raw.constData(), 61440), 0);
}

TEST(GuiPlay, PauseResetAndPowerCycle)
{
    MainWindow w;
    openGame(w);
    EXPECT_EQ(call(w, "gui.toggle_pause")["runState"], "Paused");
    EXPECT_EQ(runState(w), "Paused");
    EXPECT_EQ(call(w, "gui.toggle_pause")["runState"], "Running");

    call(w, "gui.reset");
    EXPECT_EQ(runState(w), "Running");
    call(w, "gui.power_cycle");
    EXPECT_EQ(runState(w), "Running") << "a game being played keeps playing";
    ASSERT_TRUE(waitUntil([&] { return w.windowTitle().endsWith(QStringLiteral("— cuteNES"))
        && !w.windowTitle().contains(QStringLiteral("paused")); }));
}

// Spec US1-4: an unsupported file shows the core's reason and the game
// already running keeps running.
TEST(GuiPlay, AnInvalidRomShowsWhyAndKeepsTheRunningGame)
{
    MainWindow w;
    openGame(w);
    QTemporaryDir dir;
    const QString bad = dir.filePath(QStringLiteral("bad.nes"));
    QFile file(bad);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write("not a ROM at all");
    file.close();

    QString failed;
    QObject::connect(&w, &MainWindow::commandFailed, [&](const QString& name, const QString&) { failed = name; });
    w.openRom(bad);
    ASSERT_TRUE(waitUntil([&] { return !failed.isEmpty(); }));
    EXPECT_EQ(failed, QStringLiteral("gui.open_rom"));
    EXPECT_NE(w.findChild<QMessageBox*>(QStringLiteral("commandError")), nullptr);
    EXPECT_EQ(runState(w), "Running");
    EXPECT_TRUE(w.windowTitle().contains(QStringLiteral("01-basics.nes")));

    const Result missing = w.session().call("gui.open_rom", {{"path", dir.filePath(QStringLiteral("none.nes")).toStdString()}});
    ASSERT_TRUE(missing.isError());
    EXPECT_EQ(missing.error->name, "InvalidArgument");
}

TEST(GuiPlay, ClosingTheGameClearsTheViewAndDisablesGameActions)
{
    MainWindow w;
    openGame(w);
    // The window is hidden, so nothing paints: take frames by hand.
    ASSERT_TRUE(waitUntil([&] { return w.view()->takeFrame() || !w.view()->shownPixels().empty(); }));
    call(w, "gui.close_game");
    EXPECT_EQ(runState(w), "NeedsRom");
    ASSERT_TRUE(waitUntil([&] { return !action(w, "gui.reset")->isEnabled(); }));
    w.view()->takeFrame();
    EXPECT_TRUE(w.view()->shownPixels().empty());
    EXPECT_EQ(w.windowTitle(), QStringLiteral("cuteNES"));
}

// A menu action runs its command through the session (R11).
TEST(GuiPlay, MenuActionsRunTheirCommands)
{
    MainWindow w;
    openGame(w);
    ASSERT_TRUE(waitUntil([&] { return action(w, "gui.toggle_pause")->isEnabled(); }));
    action(w, "gui.toggle_pause")->trigger();
    ASSERT_TRUE(waitUntil([&] { return runState(w) == "Paused"; }));
    ASSERT_TRUE(waitUntil([&] { return w.windowTitle().contains(QStringLiteral("paused")); }));
}

TEST(GuiPlay, ExitClosesTheWindow)
{
    MainWindow w;
    w.show();
    call(w, "gui.exit");
    EXPECT_TRUE(waitUntil([&] { return !w.isVisible(); }));
}

// FR-031: a held key reaches the game at the next frame.
TEST(GuiPlay, KeysReachTheController)
{
    MainWindow w;
    w.show();
    w.activateWindow();
    ASSERT_TRUE(waitUntil([&] { return QApplication::activeWindow() == &w; }));
    openGame(w);

    QKeyEvent press(QEvent::KeyPress, Qt::Key_X, Qt::NoModifier);
    QApplication::sendEvent(w.view(), &press);
    ASSERT_TRUE(waitUntil([&] {
        return call(w, "core.get_controller", {{"port", 1}})["buttons"] == Json::array({"A"});
    }));
    QKeyEvent release(QEvent::KeyRelease, Qt::Key_X, Qt::NoModifier);
    QApplication::sendEvent(w.view(), &release);
    ASSERT_TRUE(waitUntil([&] {
        return call(w, "core.get_controller", {{"port", 1}})["buttons"] == Json::array();
    }));
}

// contracts/cli.md
TEST(GuiCli, ParsesTheRomAndFullscreen)
{
    const auto cli = cutenes::app::parseCommandLine({QStringLiteral("cutenes"),
        QStringLiteral("--fullscreen"), QStringLiteral("game.nes")});
    EXPECT_FALSE(cli.exitCode.has_value());
    EXPECT_EQ(cli.rom, QStringLiteral("game.nes"));
    EXPECT_TRUE(cli.fullscreen);
    EXPECT_TRUE(cutenes::app::parseCommandLine({QStringLiteral("cutenes")}).rom.isEmpty());
}

TEST(GuiCli, HelpAndVersionExitZeroAndErrorsExitTwo)
{
    using cutenes::app::parseCommandLine;
    EXPECT_EQ(parseCommandLine({QStringLiteral("cutenes"), QStringLiteral("--help")}).exitCode, 0);
    const auto version = parseCommandLine({QStringLiteral("cutenes"), QStringLiteral("--version")});
    EXPECT_EQ(version.exitCode, 0);
    EXPECT_TRUE(version.output.contains(QStringLiteral(CUTENES_APP_VERSION)));
    const auto unknown = parseCommandLine({QStringLiteral("cutenes"), QStringLiteral("--bogus")});
    EXPECT_EQ(unknown.exitCode, 2);
    EXPECT_TRUE(unknown.output.contains(QStringLiteral("Usage")));
    EXPECT_EQ(parseCommandLine({QStringLiteral("cutenes"), QStringLiteral("a.nes"), QStringLiteral("b.nes")})
                  .exitCode, 2);
}
