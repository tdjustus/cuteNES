// G094 (spec FR-020 "Help"; contract "Help"): the user guide.
// - docs/user-guide.md is the guide. Help > User Guide shows that file,
//   built into the application so it works offline and matches the version.
// - Its keyboard shortcut list is the contract's: every menu item the
//   contract gives a shortcut, with that shortcut, and the game controls'
//   defaults table. Expectations come from the contract
//   (menus-and-commands.md), so the guide cannot drift from what the menus do.

#include "gui_support.hpp"
#include "mainwindow.hpp"

#include <QDialog>
#include <QFile>
#include <QTextBrowser>
#include <QTextDocument>

#include <gtest/gtest.h>

#include <set>
#include <utility>

using cutenes::api::Json;
using cutenes::api::Result;
using cutenes::app::MainWindow;
using cutenes::test::waitUntil;
using cutenes::test::windowOptions;

namespace {

const QString kContract = QStringLiteral(CUTENES_CONTRACTS "/menus-and-commands.md");
const QString kGuide = QStringLiteral(CUTENES_DOCS "/user-guide.md");

QString text(const QString& path)
{
    QFile file(path);
    EXPECT_TRUE(file.open(QIODevice::ReadOnly | QIODevice::Text)) << path.toStdString();
    return QString::fromUtf8(file.readAll());
}

// A Markdown heading's level ("### Menu shortcuts" is 3), 0 for other lines.
qsizetype headingLevel(const QString& line)
{
    qsizetype n = 0;
    while (n < line.size() && line[n] == QLatin1Char('#')) ++n;
    return n > 0 && n < line.size() && line[n] == QLatin1Char(' ') ? n : 0;
}

// The cells of each table row between the heading that starts with `from`
// and the next heading of its level or above, header and rule left out.
std::vector<QStringList> tableRows(const QString& markdown, const QString& from)
{
    std::vector<QStringList> rows;
    const qsizetype level = headingLevel(from);
    bool in = false;
    bool header = true;
    for (const QString& line : markdown.split(QLatin1Char('\n'))) {
        if (line.startsWith(from)) {
            in = true;
            header = true;
            continue;
        }
        const qsizetype heading = headingLevel(line);
        if (in && heading > 0 && heading <= level) break;
        if (!in || !line.startsWith(QLatin1Char('|'))) continue;
        if (header) { // the header row, then the |---| rule
            if (line.startsWith(QLatin1String("|-")) || line.startsWith(QLatin1String("|:"))) header = false;
            continue;
        }
        QStringList cells = line.split(QLatin1Char('|'));
        cells = cells.mid(1, cells.size() - 2);
        for (QString& cell : cells) cell = cell.trimmed();
        rows.push_back(cells);
    }
    return rows;
}

using Shortcut = std::pair<QString, QString>; // item, shortcut

// The contract's menu tables, File to Help: | Item | Command | Arguments | Shortcut | Game |.
std::set<Shortcut> contractShortcuts()
{
    std::set<Shortcut> out;
    const QString contract = text(kContract);
    for (const char* menu : {"## File", "## Emulation", "## View", "## Tools", "## Debug", "## Settings", "## Help"}) {
        const auto rows = tableRows(contract, QLatin1String(menu));
        EXPECT_FALSE(rows.empty()) << menu;
        for (const QStringList& row : rows) {
            if (row.size() == 5 && !row[3].isEmpty()) out.insert({row[0], row[3]});
        }
    }
    return out;
}

} // namespace

TEST(GuiUserGuide, TheShortcutListIsTheContracts)
{
    const std::set<Shortcut> expected = contractShortcuts();
    ASSERT_GE(expected.size(), 20u); // the File menu alone has nine

    // The guide's table: | Menu | Item | Shortcut |.
    std::set<Shortcut> listed;
    for (const QStringList& row : tableRows(text(kGuide), QStringLiteral("### Menu shortcuts"))) {
        ASSERT_EQ(row.size(), 3) << row.join(QLatin1Char('|')).toStdString();
        EXPECT_TRUE(listed.insert({row[1], row[2]}).second) << "listed twice: " << row[1].toStdString();
    }
    for (const Shortcut& s : expected) {
        EXPECT_TRUE(listed.count(s)) << "missing: " << s.first.toStdString() << " = " << s.second.toStdString();
    }
    for (const Shortcut& s : listed) {
        EXPECT_TRUE(expected.count(s)) << "not the contract's: " << s.first.toStdString() << " = "
                                       << s.second.toStdString();
    }
}

TEST(GuiUserGuide, TheGameControlsAreTheContractsDefaults)
{
    const auto expected = tableRows(text(kContract), QStringLiteral("## Game input defaults"));
    ASSERT_EQ(expected.size(), 4u);
    EXPECT_EQ(tableRows(text(kGuide), QStringLiteral("### Game controls")), expected);
}

TEST(GuiUserGuide, HelpUserGuideShowsTheGuide)
{
    MainWindow w(windowOptions());
    w.show();
    const Result r = w.session().call("gui.show_user_guide", Json::object());
    ASSERT_FALSE(r.isError()) << r.error->message;

    QDialog* window = nullptr;
    ASSERT_TRUE(waitUntil([&] {
        window = w.findChild<QDialog*>(QStringLiteral("userGuide"));
        return window && window->isVisible();
    }));
    auto* browser = window->findChild<QTextBrowser*>(QStringLiteral("userGuideText"));
    ASSERT_TRUE(browser);
    // The file in the docs folder, as Qt renders its Markdown: the copy
    // built into the application is that file.
    QTextDocument guide;
    guide.setMarkdown(text(kGuide));
    EXPECT_EQ(browser->toPlainText(), guide.toPlainText());
    EXPECT_TRUE(browser->toPlainText().startsWith(QStringLiteral("cuteNES User Guide")));
    EXPECT_TRUE(browser->openExternalLinks());

    // A window like the others: listed, and closed by name.
    ASSERT_TRUE(waitUntil([&] {
        return w.session().call("gui.list_windows", Json::object()).value["windows"] == Json::array({"user_guide"});
    }));
    EXPECT_EQ(w.session().call("gui.close_window", {{"window", "user_guide"}}).value["closed"],
        Json::array({"user_guide"}));
    EXPECT_TRUE(waitUntil([&] { return !window->isVisible(); }));

    // Showing it again brings back the one window.
    ASSERT_FALSE(w.session().call("gui.show_user_guide", Json::object()).isError());
    ASSERT_TRUE(waitUntil([&] { return window->isVisible(); }));
    EXPECT_EQ(w.findChildren<QDialog*>(QStringLiteral("userGuide")).size(), 1);
}
