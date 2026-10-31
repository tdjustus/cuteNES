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
