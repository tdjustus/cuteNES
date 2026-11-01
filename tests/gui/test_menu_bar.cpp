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

using cutenes::api::Json;
using cutenes::api::Result;
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

Json toJson(const QVariantMap& map)
{
    Json j = Json::object();
    for (auto it = map.begin(); it != map.end(); ++it) {
        const QVariant& v = it.value();
        if (v.typeId() == QMetaType::Int) {
            j[it.key().toStdString()] = v.toInt();
        } else {
            j[it.key().toStdString()] = v.toString().toStdString();
        }
    }
    return j;
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

TEST(GuiMenuBar, LaterItemsAreDisabledSayWhyAndReturnNotImplemented)
{
    MainWindow w(windowOptions());
    w.show();
    ASSERT_FALSE(w.session().call("gui.open_rom", {{"path", kRom.toStdString()}}).isError());
    ASSERT_TRUE(waitUntil([&] { return w.findChild<QAction*>(QStringLiteral("gui.take_screenshot"))->isEnabled(); }));
    int later = 0;
    for (QAction* a : menuActions(w)) {
        if (a->isEnabled()) continue;
        ++later;
        const std::string command = a->property("command").toString().toStdString();
        EXPECT_EQ(a->toolTip(), QStringLiteral("Coming in a later update")) << command;
        const Result r = w.session().call(command, toJson(a->property("commandArgs").toMap()));
        ASSERT_TRUE(r.isError()) << command;
        EXPECT_EQ(r.error->name, "NotImplemented") << command << ": " << r.error->message;
    }
    EXPECT_GT(later, 0) << "phases G4-G9 are still to come";
}
