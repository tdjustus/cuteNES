// G005: the application skeleton. Expectations come from the spec (US3-1,
// FR-020 menu names and order; FR-024 platform roles) and the menus
// contract (File > Exit is Ctrl+Q, Help > About; each action carries its
// gui.* command).

#include "about_dialog.hpp"
#include "mainwindow.hpp"

#include <cutenes/api/registry.hpp>
#include <cutenes/qt/media_runtime.hpp>
#include <cutenes/version.hpp>

#include <QAction>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QSet>
#include <QTabWidget>

#include <gtest/gtest.h>

using cutenes::app::AboutDialog;
using cutenes::app::MainWindow;

namespace {

QString plain(QString title)
{
    return title.remove(QLatin1Char('&'));
}

QAction* actionFor(const QMenu* menu, const char* command)
{
    for (QAction* a : menu->actions()) {
        if (a->property("command").toString() == QLatin1String(command)) return a;
    }
    return nullptr;
}

} // namespace

TEST(GuiSkeleton, MainWindowIsTitledCuteNes)
{
    MainWindow w;
    EXPECT_EQ(w.windowTitle(), QStringLiteral("cuteNES"));
}

TEST(GuiSkeleton, MenuBarHasTheSevenMenusInOrder)
{
    MainWindow w;
    QStringList titles;
    for (QAction* a : w.menuBar()->actions()) titles << plain(a->text());
    EXPECT_EQ(titles, (QStringList{QStringLiteral("File"), QStringLiteral("Emulation"),
                          QStringLiteral("View"), QStringLiteral("Tools"), QStringLiteral("Debug"),
                          QStringLiteral("Settings"), QStringLiteral("Help")}));
}

TEST(GuiSkeleton, ExitAndAboutCarryTheirCommandsShortcutsAndRoles)
{
    MainWindow w;
    QAction* exitAction = actionFor(w.menu(QStringLiteral("menuFile")), "gui.exit");
    ASSERT_NE(exitAction, nullptr);
    EXPECT_EQ(exitAction->shortcut(), QKeySequence(Qt::CTRL | Qt::Key_Q));
    EXPECT_EQ(exitAction->menuRole(), QAction::QuitRole);

    QAction* about = actionFor(w.menu(QStringLiteral("menuHelp")), "gui.show_about");
    ASSERT_NE(about, nullptr);
    EXPECT_EQ(about->menuRole(), QAction::AboutRole);
}

TEST(GuiSkeleton, AboutShowsTheVersionsAndTheLicenses)
{
    MainWindow w;
    actionFor(w.menu(QStringLiteral("menuHelp")), "gui.show_about")->trigger();
    auto* dialog = w.findChild<AboutDialog*>(QStringLiteral("aboutDialog"));
    ASSERT_NE(dialog, nullptr);
    EXPECT_TRUE(dialog->isVisible()); // non-modal: trigger() returned while it is open

    const QString versions = dialog->findChild<QLabel*>(QStringLiteral("versions"))->text();
    EXPECT_TRUE(versions.contains(QStringLiteral(CUTENES_APP_VERSION)));
    EXPECT_TRUE(versions.contains(QString::fromLatin1(cutenes::coreVersionString())));
    EXPECT_TRUE(versions.contains(QString::fromStdString(cutenes::api::surfaceVersion())));

    // Qt is LGPL-3.0 and its text must ship (constitution, research R1).
    auto* tabs = dialog->findChild<QTabWidget*>(QStringLiteral("licenses"));
    ASSERT_NE(tabs, nullptr);
    QStringList names;
    for (int i = 0; i < tabs->count(); ++i) names << tabs->tabText(i);
    for (const char* name : {"cuteNES", "Qt", "SDL3"}) {
        EXPECT_TRUE(names.contains(QString::fromLatin1(name))) << name;
    }
    const auto* qtText = tabs->widget(static_cast<int>(names.indexOf(QStringLiteral("Qt"))));
    EXPECT_TRUE(qtText->property("plainText").toString().contains(
        QStringLiteral("GNU LESSER GENERAL PUBLIC LICENSE")));
}

// SDL's dummy audio driver always starts (SDL3 docs, "Audio drivers"); the
// gamepad subsystem needs no device to start.
TEST(GuiMediaRuntime, StartsAudioAndGamepadsHeadless)
{
    const cutenes::qt::MediaRuntime media;
    EXPECT_TRUE(media.audioAvailable()) << media.error();
    EXPECT_EQ(media.audioDriver(), "dummy");
    EXPECT_TRUE(media.gamepadsAvailable()) << media.error();
}

#if !defined(_WIN32)
#include <csignal>

// SDL must leave SIGTERM to the process (MediaRuntime: Qt owns the
// lifecycle), so the default action, terminating, still applies.
TEST(GuiMediaRuntime, LeavesSigtermToTheProcess)
{
    const cutenes::qt::MediaRuntime media;
    struct sigaction current {};
    ASSERT_EQ(sigaction(SIGTERM, nullptr, &current), 0);
    EXPECT_EQ(current.sa_handler, SIG_DFL);
}
#endif

// The menu inventory (contracts/menus-and-commands.md): every command in the
// contract's menu tables, and no other, is on some menu action. Open
// Recent's entries (gui.open_recent) appear only once there are recent games.
TEST(GuiMenus, EveryContractCommandIsOnTheMenus)
{
    MainWindow w;
    QSet<QString> found;
    for (QAction* a : w.menuBar()->findChildren<QAction*>()) {
        const QString command = a->property("command").toString();
        if (!command.isEmpty()) found.insert(command);
    }
    const QSet<QString> expected = {
        // File
        "gui.open_rom", "gui.clear_recent", "gui.show_library", "gui.close_game",
        "gui.save_state", "gui.load_state", "gui.quick_save", "gui.quick_load",
        "gui.select_slot", "gui.undo_load_state", "gui.save_state_file", "gui.load_state_file",
        "gui.import_battery_save", "gui.export_battery_save", "gui.take_screenshot",
        "gui.open_library_folder", "gui.exit",
        // Emulation
        "gui.toggle_pause", "gui.reset", "gui.power_cycle", "gui.frame_advance",
        "gui.set_fast_forward", "gui.toggle_fast_forward", "gui.set_rewind", "gui.set_speed",
        "gui.set_region",
        // View
        "gui.set_scale", "gui.toggle_fullscreen", "gui.toggle_integer_scaling", "gui.set_aspect",
        "gui.set_overscan", "gui.set_filter", "gui.set_palette", "gui.toggle_fps",
        "gui.toggle_osd", "gui.toggle_always_on_top",
        // Tools
        "gui.show_cheats", "gui.show_cheat_search", "gui.record_movie", "gui.play_movie",
        "gui.stop_movie", "gui.toggle_movie_read_only", "gui.toggle_audio_recording",
        // Debug
        "gui.show_debugger", "gui.show_memory_viewer", "gui.show_ppu_viewer",
        "gui.show_trace_logger", "gui.write_memory_dump",
        // Settings
        "gui.show_settings",
        // Help
        "gui.show_shortcuts", "gui.show_user_guide", "gui.open_log_folder", "gui.show_about",
    };
    EXPECT_EQ(found, expected) << "missing: " << (expected - found).values().join(", ").toStdString()
                               << "; extra: " << (found - expected).values().join(", ").toStdString();
}

// macOS hides an empty top-level menu, and moves Quit, About and Settings
// to the application menu (FR-024). A menu holding only role items would
// vanish, as File and Help did in G0's first draft, so each menu must keep
// an item of its own (contract "Conventions").
TEST(GuiMenus, EveryMenuKeepsAnItemOfItsOwnOnMacOs)
{
    MainWindow w;
    for (QAction* top : w.menuBar()->actions()) {
        int own = 0;
        for (QAction* a : top->menu()->actions()) {
            if (!a->isSeparator() && a->menuRole() == QAction::NoRole) ++own;
        }
        EXPECT_GT(own, 0) << plain(top->text()).toStdString();
    }
}

namespace {

QAction* actionWith(const MainWindow& w, const char* command, const char* arg = nullptr,
    const QVariant& value = {})
{
    for (QAction* a : w.menuBar()->findChildren<QAction*>()) {
        if (a->property("command").toString() != QLatin1String(command)) continue;
        if (arg == nullptr || a->property("commandArgs").toMap().value(QLatin1String(arg)) == value) {
            return a;
        }
    }
    return nullptr;
}

} // namespace

// Default shortcuts from the contract's tables.
TEST(GuiMenus, DefaultShortcutsFollowTheContract)
{
    MainWindow w;
    const struct {
        const char* command;
        const char* arg;
        QVariant value;
        QKeySequence key;
    } cases[] = {
        {"gui.open_rom", nullptr, {}, QKeySequence(Qt::CTRL | Qt::Key_O)},
        {"gui.show_library", nullptr, {}, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O)},
        {"gui.save_state", "slot", 1, QKeySequence(Qt::SHIFT | Qt::Key_F1)},
        {"gui.save_state", "slot", 10, QKeySequence(Qt::SHIFT | Qt::Key_F10)},
        {"gui.load_state", "slot", 1, QKeySequence(Qt::Key_F1)},
        {"gui.select_slot", "slot", 10, QKeySequence(Qt::CTRL | Qt::Key_0)},
        {"gui.quick_save", nullptr, {}, QKeySequence(Qt::CTRL | Qt::Key_S)},
        {"gui.quick_load", nullptr, {}, QKeySequence(Qt::CTRL | Qt::Key_L)},
        {"gui.undo_load_state", nullptr, {}, QKeySequence(Qt::CTRL | Qt::Key_Z)},
        {"gui.take_screenshot", nullptr, {}, QKeySequence(Qt::Key_F12)},
        {"gui.toggle_pause", nullptr, {}, QKeySequence(Qt::Key_P)},
        {"gui.reset", nullptr, {}, QKeySequence(Qt::CTRL | Qt::Key_R)},
        {"gui.power_cycle", nullptr, {}, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_R)},
        {"gui.frame_advance", nullptr, {}, QKeySequence(Qt::Key_Backslash)},
        {"gui.set_scale", "scale", 3, QKeySequence(Qt::ALT | Qt::Key_3)},
        {"gui.toggle_fullscreen", nullptr, {}, QKeySequence(Qt::Key_F11)},
        {"gui.show_debugger", nullptr, {}, QKeySequence(Qt::CTRL | Qt::Key_D)},
        {"gui.show_settings", nullptr, {}, QKeySequence(Qt::CTRL | Qt::Key_Comma)},
        {"gui.exit", nullptr, {}, QKeySequence(Qt::CTRL | Qt::Key_Q)},
    };
    for (const auto& c : cases) {
        const QAction* a = actionWith(w, c.command, c.arg, c.value);
        ASSERT_NE(a, nullptr) << c.command;
        EXPECT_EQ(a->shortcut(), c.key) << c.command << " " << a->text().toStdString();
    }
}

// Until its phase lands, an item is present but disabled (contract
// "Conventions"); G0 implements only Exit and About.
TEST(GuiMenus, OnlyImplementedItemsAreEnabled)
{
    MainWindow w;
    for (QAction* a : w.menuBar()->findChildren<QAction*>()) {
        const QString command = a->property("command").toString();
        if (command.isEmpty()) continue;
        const bool implemented = command == QLatin1String("gui.exit")
            || command == QLatin1String("gui.show_about");
        EXPECT_EQ(a->isEnabled(), implemented) << command.toStdString();
    }
}
