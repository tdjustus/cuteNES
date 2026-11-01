// G090, the gui-audit test (quickstart A8, spec FR-020, FR-021, FR-070,
// SC-003). The menu inventory, contracts/menus-and-commands.md, is the
// authority: this test reads that file itself, so a change to the menus, the
// catalog or the contract that leaves the other two behind fails here.
// - The menu actions' commands are the commands of the contract's menu
//   tables (## File to ## Help), no more and no fewer.
// - Each action's shortcuts are the ones its rows give.
// - The Game column is the catalog's needsRom; without a game the game-only
//   actions are disabled and the others enabled.
// - Every action is wired: its command is in the catalog, and its arguments
//   are valid input for it. Or it is a later phase's item: disabled, with
//   the tooltip "Coming in a later update", its command NotImplemented.
// - The gui.* catalog is the gui.* commands the contract names anywhere
//   (`gui.trace_*` style names stand for a family), no more and no fewer.

#include "gui_support.hpp"
#include "mainwindow.hpp"

#include <cutenes/api/schema.hpp>

#include <QAction>
#include <QFile>
#include <QJsonDocument>
#include <QMenuBar>
#include <QRegularExpression>
#include <QSet>

#include <gtest/gtest.h>

#include <map>

using cutenes::api::Json;
using cutenes::api::Result;
using cutenes::app::MainWindow;
using cutenes::test::windowOptions;

namespace {

const QString kContract = QStringLiteral(CUTENES_CONTRACTS "/menus-and-commands.md");
const QString kNestest = QStringLiteral(CUTENES_TEST_ROMS "/other/nestest.nes");

QString contractText()
{
    QFile file(kContract);
    EXPECT_TRUE(file.open(QIODevice::ReadOnly | QIODevice::Text)) << kContract.toStdString();
    return QString::fromUtf8(file.readAll());
}

// One row of a menu table: | Item | Command | Arguments | Shortcut | Game |.
struct Row {
    QString item;
    QString command;
    QString shortcut;
    QString game; // "✓", "movie" or empty
};

QString cell(QString text)
{
    text = text.trimmed();
    if (text.startsWith(QLatin1Char('`')) && text.endsWith(QLatin1Char('`'))) text = text.mid(1, text.size() - 2);
    return text;
}

// The rows of the tables between "## File" and "## Items from later phases".
std::vector<Row> menuRows()
{
    std::vector<Row> rows;
    bool inMenus = false;
    for (const QString& line : contractText().split(QLatin1Char('\n'))) {
        if (line.startsWith(QLatin1String("## File"))) inMenus = true;
        if (line.startsWith(QLatin1String("## Items from later phases"))) inMenus = false;
        if (!inMenus || !line.startsWith(QLatin1String("| ")) || line.startsWith(QLatin1String("| Item"))) continue;
        // Split on the bars between cells; "\|" never occurs in these tables.
        QStringList cells = line.split(QLatin1Char('|'));
        if (cells.size() < 7) continue;
        rows.push_back({cells[1].trimmed(), cell(cells[2]), cells[4].trimmed(), cells[5].trimmed()});
    }
    return rows;
}

// The keys a Shortcut cell lists, in Qt's portable text: "P, Pause",
// "Tab (held)", "− / =", "Shift+F1 … Shift+F10", "Ctrl+1 … Ctrl+0".
QStringList keys(QString text)
{
    text.replace(QStringLiteral(" (held)"), QString());
    text.replace(QStringLiteral("\\\\"), QStringLiteral("\\")); // markdown's escaped backslash
    text.replace(QStringLiteral("−"), QStringLiteral("-"));     // the minus sign the table prints
    // Enter is the main keyboard's key, Qt's Return (its Enter is the
    // keypad's).
    text.replace(QStringLiteral("Enter"), QStringLiteral("Return"));
    QStringList out;
    // Keys are listed with ", " or " / " between them; "Ctrl+," is one key.
    for (QString part : text.split(QRegularExpression(QStringLiteral(",\\s+|\\s*/\\s*")), Qt::SkipEmptyParts)) {
        part = part.trimmed();
        const QRegularExpression range(QStringLiteral("^(.*?)(F?)(\\d+) … \\1\\2(\\d+)$"));
        const QRegularExpressionMatch m = range.match(part);
        if (!m.hasMatch()) {
            out << part;
            continue;
        }
        // A run of function keys or digits; "1 … 0" ends at 0 after 9.
        const int first = m.captured(3).toInt();
        const int last = m.captured(4).toInt();
        for (int n = first; n != (last == 0 ? 11 : last + 1); ++n) {
            out << m.captured(1) + m.captured(2) + QString::number(m.captured(2).isEmpty() ? n % 10 : n);
        }
    }
    return out;
}

QList<QAction*> menuActions(const MainWindow& w)
{
    QList<QAction*> out;
    for (QAction* a : w.menuBar()->findChildren<QAction*>()) {
        if (!a->property("command").toString().isEmpty()) out << a;
    }
    return out;
}

std::map<std::string, Json> catalog(MainWindow& w)
{
    const Result r = w.session().call("api.catalog", Json::object());
    EXPECT_FALSE(r.isError());
    std::map<std::string, Json> out;
    for (const Json& c : r.value["commands"]) out[c["name"].get<std::string>()] = c;
    return out;
}

Json toJson(const QVariantMap& map)
{
    return Json::parse(QJsonDocument::fromVariant(map).toJson(QJsonDocument::Compact).toStdString());
}

QString names(const QSet<QString>& set)
{
    QStringList list = set.values();
    list.sort();
    return list.join(QStringLiteral(", "));
}

// A window with a game in its recent list (so Open Recent has an entry)
// and none open.
void withRecentGame(MainWindow& w)
{
    w.session().call("gui.set_setting", {{"key", "emulation.pauseWhenInactive"}, {"value", false}});
    EXPECT_FALSE(w.session().call("gui.open_rom", {{"path", kNestest.toStdString()}}).isError());
    EXPECT_FALSE(w.session().call("gui.close_game", Json::object()).isError());
    EXPECT_TRUE(cutenes::test::waitUntil([&] {
        for (QAction* a : menuActions(w)) {
            if (a->property("command") == QStringLiteral("gui.open_recent")) return true;
        }
        return false;
    }));
}

} // namespace

TEST(GuiAudit, TheMenuActionsAreTheContractsTables)
{
    const std::vector<Row> rows = menuRows();
    ASSERT_GT(rows.size(), 50u) << "the contract's tables were not found";
    MainWindow w(windowOptions());
    withRecentGame(w);

    QSet<QString> contract;
    for (const Row& r : rows) contract.insert(r.command);
    QSet<QString> menus;
    for (QAction* a : menuActions(w)) menus.insert(a->property("command").toString());
    EXPECT_EQ(menus, contract) << "on the menus only: " << names(menus - contract).toStdString()
                               << "; in the contract only: " << names(contract - menus).toStdString();

    // Each table row has its own action: a command's rows and actions are
    // as many (an action per slot, scale or speed counts as its row's).
    std::map<QString, int> rowCount;
    for (const Row& r : rows) ++rowCount[r.command];
    for (const auto& [command, count] : rowCount) {
        int actions = 0;
        for (QAction* a : menuActions(w)) actions += a->property("command").toString() == command;
        EXPECT_GE(actions, count) << command.toStdString();
    }
}

TEST(GuiAudit, EachActionHasTheShortcutsItsRowsGive)
{
    MainWindow w(windowOptions());
    std::map<QString, QSet<QString>> contract; // command: its rows' keys
    for (const Row& r : menuRows()) {
        for (const QString& key : keys(r.shortcut)) contract[r.command].insert(key);
    }
    std::map<QString, QSet<QString>> menus;
    for (QAction* a : menuActions(w)) {
        for (const QKeySequence& key : a->shortcuts()) {
            menus[a->property("command").toString()].insert(key.toString(QKeySequence::PortableText));
        }
    }
    QSet<QString> commands;
    for (const auto& [c, k] : contract) commands.insert(c);
    for (const auto& [c, k] : menus) commands.insert(c);
    for (const QString& c : commands) {
        // Each side parsed the same way, so "Ctrl+," and Qt's "Ctrl+,"
        // compare as text.
        QSet<QString> want;
        for (const QString& k : contract[c]) want.insert(QKeySequence::fromString(k, QKeySequence::PortableText).toString(QKeySequence::PortableText));
        EXPECT_EQ(menus[c], want) << c.toStdString() << ": the menus have " << names(menus[c]).toStdString()
                                  << ", the contract " << names(want).toStdString();
    }
}

TEST(GuiAudit, TheGameColumnIsTheCatalogsAndDisablesWithoutAGame)
{
    MainWindow w(windowOptions());
    withRecentGame(w);
    const auto commands = catalog(w);
    std::map<QString, QString> game;
    for (const Row& r : menuRows()) game[r.command] = r.game;
    for (const auto& [command, column] : game) {
        const auto it = commands.find(command.toStdString());
        ASSERT_NE(it, commands.end()) << command.toStdString();
        EXPECT_EQ(it->second["needsRom"].get<bool>(), !column.isEmpty()) << command.toStdString() << " (Game: "
                                                                          << column.toStdString() << ")";
    }
    // No game open: a game-only item is disabled; the others are enabled,
    // but for a later phase's items and Clear List with no recent games.
    for (QAction* a : menuActions(w)) {
        const QString command = a->property("command").toString();
        if (a->toolTip() == QStringLiteral("Coming in a later update")) {
            EXPECT_FALSE(a->isEnabled()) << command.toStdString();
            continue;
        }
        EXPECT_EQ(a->isEnabled(), game[command].isEmpty()) << command.toStdString() << " " << a->text().toStdString();
    }
}

TEST(GuiAudit, EveryActionIsWiredOrALaterPhasesItem)
{
    MainWindow w(windowOptions());
    withRecentGame(w);
    const auto commands = catalog(w);
    for (QAction* a : menuActions(w)) {
        const std::string command = a->property("command").toString().toStdString();
        const auto it = commands.find(command);
        ASSERT_NE(it, commands.end()) << command << " is not in the catalog";
        const Json args = toJson(a->property("commandArgs").toMap());
        const auto failure = cutenes::api::validate(it->second["inputSchema"], args);
        EXPECT_FALSE(failure) << command << " " << a->text().toStdString() << ": " << args.dump() << " "
                              << (failure ? failure->path + " " + failure->message : "");
        if (a->toolTip() == QStringLiteral("Coming in a later update")) {
            // A later phase's: its command says so (contract "Items from
            // later phases").
            const Result r = w.session().call(command, args);
            ASSERT_TRUE(r.isError()) << command;
            EXPECT_EQ(r.error->name, "NotImplemented") << command;
            EXPECT_TRUE(r.error->data.contains("phase")) << command;
        }
    }
}

TEST(GuiAudit, TheGuiCatalogIsTheCommandsTheContractNames)
{
    // Every `gui.…` in the contract; one with a * names a family.
    QSet<QString> named;
    QList<QRegularExpression> families;
    const QRegularExpression mention(QStringLiteral("`(gui\\.[a-z_*]+)`"));
    for (auto it = mention.globalMatch(contractText()); it.hasNext();) {
        const QString name = it.next().captured(1);
        if (name.contains(QLatin1Char('*'))) {
            families << QRegularExpression(QStringLiteral("^") + QRegularExpression::escape(name).replace(QStringLiteral("\\*"), QStringLiteral("[a-z_]+")) + QStringLiteral("$"));
        } else {
            named.insert(name);
        }
    }
    MainWindow w(windowOptions());
    QSet<QString> gui;
    for (const auto& [name, c] : catalog(w)) {
        if (name.rfind("gui.", 0) == 0) gui.insert(QString::fromStdString(name));
    }
    QSet<QString> unnamed; // in the catalog, in no row and no family
    for (const QString& c : gui) {
        bool inFamily = false;
        for (const QRegularExpression& f : families) inFamily = inFamily || f.match(c).hasMatch();
        if (!named.contains(c) && !inFamily) unnamed.insert(c);
    }
    EXPECT_TRUE(unnamed.isEmpty()) << "in the catalog, not the contract: " << names(unnamed).toStdString();
    EXPECT_TRUE((named - gui).isEmpty()) << "in the contract, not the catalog: " << names(named - gui).toStdString();
    for (const QRegularExpression& f : families) {
        bool any = false;
        for (const QString& c : gui) any = any || f.match(c).hasMatch();
        EXPECT_TRUE(any) << "no command in the family " << f.pattern().toStdString();
    }
}
