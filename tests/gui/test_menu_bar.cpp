// G034: the complete menu bar per contracts/menus-and-commands.md. Every
// command in the contract's menu tables has a menu item and a gui.* command
// in the catalog. Items whose feature lands in a later phase are present but
// disabled, with the tooltip "Coming in a later update", and their commands
// return NotImplemented until then.

#include "gui_support.hpp"
#include "mainwindow.hpp"

#include <QAction>
#include <QFile>
#include <QMenuBar>
#include <QRegularExpression>
#include <QSet>

#include <gtest/gtest.h>

using cutenes::app::MainWindow;
using cutenes::test::waitUntil;
using cutenes::test::windowOptions;

namespace {

const QString kRom = QStringLiteral(CUTENES_TEST_ROMS "/instr_test-v5/rom_singles/01-basics.nes");
const QString kContract = QStringLiteral(CUTENES_TEST_ROMS "/../../specs/003-qt-frontend/contracts/menus-and-commands.md");

// The commands of the contract's menu tables: rows "| Item | `gui.x` | ...".
QSet<QString> contractMenuCommands()
{
    QFile file(kContract);
    EXPECT_TRUE(file.open(QIODevice::ReadOnly | QIODevice::Text)) << kContract.toStdString();
    QSet<QString> commands;
    const QRegularExpression row(QStringLiteral("^\\|[^|]*\\| `(gui\\.[a-z_]+)`"));
    for (const QString& line : QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'))) {
        const auto m = row.match(line);
        if (m.hasMatch()) commands.insert(m.captured(1));
    }
    return commands;
}

QList<QAction*> menuActions(MainWindow& w)
{
    QList<QAction*> actions;
    for (QAction* a : w.menuBar()->findChildren<QAction*>()) {
        if (!a->property("command").toString().isEmpty()) actions << a;
    }
    return actions;
}

} // namespace

TEST(GuiMenuBar, EveryContractItemIsInTheMenusAndTheCatalog)
{
    MainWindow w(windowOptions());
    w.show();
    // A game in File > Open Recent, so its entries (gui.open_recent) exist.
    ASSERT_FALSE(w.session().call("gui.open_rom", {{"path", kRom.toStdString()}}).isError());
    ASSERT_TRUE(waitUntil([&] { return w.findChild<QAction*>(QStringLiteral("gui.open_recent")) != nullptr; }));
    const QSet<QString> contract = contractMenuCommands();
    ASSERT_GT(contract.size(), 40) << "the contract's tables were read";
    QSet<QString> inMenus;
    for (QAction* a : menuActions(w)) inMenus.insert(a->property("command").toString());
    for (const QString& command : contract) {
        EXPECT_TRUE(inMenus.contains(command)) << "no menu item for " << command.toStdString();
        EXPECT_TRUE(w.session().registry().find(command.toStdString()).has_value())
            << command.toStdString() << " is not in the catalog";
    }
    for (const QString& command : inMenus) {
        EXPECT_TRUE(contract.contains(command)) << command.toStdString() << " is not in the contract";
    }
}

// Since Open Log Folder (G9), no item is a later phase's (contract "Items
// from later phases"): with a game, every item is enabled but the two that
// wait for a movie, and none says it is coming later.
TEST(GuiMenuBar, WithAGameEveryItemWorksButTheMovieOnes)
{
    MainWindow w(windowOptions());
    w.show();
    ASSERT_FALSE(w.session().call("gui.open_rom", {{"path", kRom.toStdString()}}).isError());
    ASSERT_TRUE(waitUntil([&] { return w.findChild<QAction*>(QStringLiteral("gui.take_screenshot"))->isEnabled(); }));
    const QStringList needMovie = {QStringLiteral("gui.stop_movie"), QStringLiteral("gui.toggle_movie_read_only")};
    int movieOnly = 0;
    for (QAction* a : menuActions(w)) {
        const QString command = a->property("command").toString();
        EXPECT_NE(a->toolTip(), QStringLiteral("Coming in a later update")) << command.toStdString();
        if (needMovie.contains(command)) {
            EXPECT_FALSE(a->isEnabled()) << command.toStdString();
            ++movieOnly;
        } else {
            EXPECT_TRUE(a->isEnabled()) << command.toStdString() << " " << a->text().toStdString();
        }
    }
    EXPECT_EQ(movieOnly, 2);
}
