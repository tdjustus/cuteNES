// #181: hiding games from the library (spec US1-7, FR-007; contracts/
// library-layout.md "Hidden games", menus-and-commands.md "Hiding games").
// Expectations come from those contracts: .hiddenroms holds `<sha1>  <name>`
// lines, comments and blank lines are kept out of the list, a bad line is
// skipped, the file is rewritten atomically and removed when empty, and a
// hidden game is left out of the library and Recent, by identity, so a
// rename keeps it hidden. No ROM file is touched.

#include "dialogs/settings_dialog.hpp"
#include "gui_support.hpp"
#include "library/hidden_games.hpp"
#include "library/library_browser.hpp"
#include "mainwindow.hpp"

#include <QAbstractItemView>
#include <QAction>
#include <QContextMenuEvent>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QPushButton>
#include <QTemporaryDir>
#include <QToolButton>

#include <gtest/gtest.h>

using cutenes::api::Json;
using cutenes::api::Result;
using cutenes::app::HiddenGames;
using cutenes::app::LibraryBrowser;
using cutenes::app::MainWindow;
using cutenes::app::SettingsDialog;
using cutenes::test::waitUntil;
using cutenes::test::windowOptions;

namespace {

const QString kRom = QStringLiteral(CUTENES_TEST_ROMS "/instr_test-v5/rom_singles/01-basics.nes");

Json call(MainWindow& w, const char* name, const Json& args = Json::object())
{
    const Result r = w.session().call(name, args);
    EXPECT_FALSE(r.isError()) << name << ": " << (r.isError() ? r.error->message : "");
    return r.value;
}

std::string error(MainWindow& w, const char* name, const Json& args)
{
    const Result r = w.session().call(name, args);
    return r.isError() ? r.error->name : std::string();
}

QByteArray readAll(const QString& path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

void put(const QString& path, const QByteArray& bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write(bytes);
}

// 01-basics with its last CHR byte changed by `variant`: another game
// (another identity) with the same program.
QString copyRom(const QString& path, char variant = 0)
{
    QByteArray rom = readAll(kRom);
    EXPECT_FALSE(rom.isEmpty());
    rom[rom.size() - 1] = static_cast<char>(rom[rom.size() - 1] ^ variant);
    put(path, rom);
    return QFileInfo(path).absoluteFilePath();
}

// The identity R7 defines: the SHA-1 of the ROM after its 16-byte header.
QString identityOf(const QString& path)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(readAll(path).mid(16), QCryptographicHash::Sha1).toHex());
}

QStringList listedNames(MainWindow& w)
{
    QStringList names;
    const Json list = call(w, "gui.library_list"); // outlives the loop
    for (const Json& e : list["entries"]) names << QString::fromStdString(e["name"].get<std::string>());
    names.sort();
    return names;
}

const QString kA = QStringLiteral("0123456789abcdef0123456789abcdef01234567");
const QString kB = QStringLiteral("89abcdef0123456789abcdef0123456789abcdef");

} // namespace

// library-layout "Hidden games", Reading.
TEST(GuiHiddenGames, TheFileIsReadLineByLine)
{
    QTemporaryDir dir;
    const QString file = HiddenGames::fileIn(dir.path());
    EXPECT_EQ(file, QDir(dir.path()).filePath(QStringLiteral(".hiddenroms")));
    EXPECT_TRUE(HiddenGames::read(file).games().isEmpty()) << "no file: nothing hidden";

    put(file, "# my hidden games\n"
              "\n"
              "0123456789ABCDEF0123456789ABCDEF01234567  Game A (USA)\n"
              "not a game line\n"
              "0123456789abcdef0123456789abcdef01234567  Game A again\n"
              "89abcdef0123456789abcdef0123456789abcdef  Game B\n");
    QStringList skipped;
    const HiddenGames hidden = HiddenGames::read(file, &skipped);
    ASSERT_EQ(hidden.games().size(), 2);
    EXPECT_EQ(hidden.games()[0].identity, kA) << "upper-case hex reads as lower-case";
    EXPECT_EQ(hidden.games()[0].name, QStringLiteral("Game A (USA)"));
    EXPECT_EQ(hidden.games()[1].identity, kB);
    EXPECT_TRUE(hidden.contains(kA));
    EXPECT_FALSE(hidden.contains(QStringLiteral("fedcba9876543210fedcba9876543210fedcba98")));
    ASSERT_EQ(skipped.size(), 1) << "the bad line; the duplicate is ignored quietly";
    EXPECT_TRUE(skipped[0].contains(QStringLiteral("line 4"))) << skipped[0].toStdString();
}

// library-layout "Hidden games", Writing.
TEST(GuiHiddenGames, WritingKeepsCommentsAndRemovesAnEmptyFile)
{
    QTemporaryDir dir;
    const QString file = HiddenGames::fileIn(dir.path());
    put(file, "# my hidden games\n" + kA.toLatin1() + "  Game A\n");
    HiddenGames hidden = HiddenGames::read(file);
    EXPECT_TRUE(hidden.hide(kB, QStringLiteral("Game B")));
    EXPECT_FALSE(hidden.hide(kB, QStringLiteral("Game B"))) << "already hidden";
    QString why;
    ASSERT_TRUE(hidden.write(file, &why)) << why.toStdString();
    EXPECT_EQ(readAll(file), "# my hidden games\n" + kA.toLatin1() + "  Game A\n" + kB.toLatin1() + "  Game B\n");
    EXPECT_TRUE(QDir(dir.path()).entryList(QDir::Files | QDir::Hidden).size() == 1) << "no temporary file left";

    EXPECT_TRUE(hidden.unhide(kA));
    EXPECT_FALSE(hidden.unhide(kA));
    EXPECT_TRUE(hidden.unhide(kB));
    ASSERT_TRUE(hidden.write(file));
    EXPECT_FALSE(QFile::exists(file)) << "no game hidden: no file, comments or not";
}

TEST(GuiHiddenGames, AHiddenGameLeavesTheLibraryAndComesBack)
{
    const auto options = windowOptions();
    const QString roms = options.libraryRoot + QStringLiteral("/roms");
    const QString a = copyRom(roms + QStringLiteral("/alpha.nes"), 0);
    const QString b = copyRom(roms + QStringLiteral("/beta.nes"), 1);
    const QByteArray romBytes = readAll(a);
    const std::string idA = identityOf(a).toStdString(); // before the rename below
    const std::string idB = identityOf(b).toStdString();
    MainWindow w(options);
    ASSERT_TRUE(waitUntil([&] { return listedNames(w).size() == 2; })) << listedNames(w).join(u',').toStdString();

    const Json result = call(w, "gui.library_hide", {{"identity", idA}});
    ASSERT_EQ(result["hidden"].size(), 1u);
    EXPECT_EQ(result["hidden"][0]["identity"], idA);
    EXPECT_EQ(result["hidden"][0]["name"], "alpha");
    EXPECT_TRUE(waitUntil([&] { return listedNames(w) == QStringList{QStringLiteral("beta")}; }));
    EXPECT_EQ(readAll(HiddenGames::fileIn(options.libraryRoot)), QByteArray::fromStdString(idA) + "  alpha\n");
    EXPECT_EQ(readAll(a), romBytes) << "the ROM is untouched";
    EXPECT_EQ(call(w, "gui.library_hidden")["hidden"], result["hidden"]);
    EXPECT_EQ(call(w, "gui.library_hide", {{"identity", idA}})["hidden"].size(), 1u)
        << "hiding it again changes nothing";

    // By identity: renamed and moved, it stays hidden.
    QDir().mkpath(roms + QStringLiteral("/sub"));
    ASSERT_TRUE(QFile::rename(a, roms + QStringLiteral("/sub/renamed.nes")));
    QFile::remove(b);
    EXPECT_TRUE(waitUntil([&] { return listedNames(w).isEmpty(); }, 8000)) << listedNames(w).join(u',').toStdString();

    EXPECT_EQ(error(w, "gui.library_unhide", {{"identities", Json::array({idB})}}), "InvalidArguments")
        << "not hidden";
    EXPECT_TRUE(call(w, "gui.library_unhide", {{"identities", Json::array({idA})}})["hidden"].empty());
    EXPECT_TRUE(waitUntil([&] { return listedNames(w) == QStringList{QStringLiteral("renamed")}; }));
    EXPECT_FALSE(QFile::exists(HiddenGames::fileIn(options.libraryRoot)));
}

TEST(GuiHiddenGames, AHiddenGameLeavesRecentUntilItIsUnhidden)
{
    const auto options = windowOptions();
    QString dir;
    windowOptions(&dir);
    const QString a = copyRom(dir + QStringLiteral("/a.nes"), 0);
    const QString b = copyRom(dir + QStringLiteral("/b.nes"), 1);
    MainWindow w(options);
    call(w, "gui.open_rom", {{"path", a.toStdString()}});
    call(w, "gui.open_rom", {{"path", b.toStdString()}});
    call(w, "gui.close_game");
    ASSERT_TRUE(waitUntil([&] { return listedNames(w).size() == 2; })) << listedNames(w).join(u',').toStdString();
    QMenu* menu = w.menuBar()->findChild<QMenu*>(QStringLiteral("menuRecent"));
    ASSERT_NE(menu, nullptr);

    call(w, "gui.library_hide", {{"identity", identityOf(b).toStdString()}});
    EXPECT_TRUE(waitUntil([&] { return menu->findChildren<QAction*>(QStringLiteral("gui.open_recent")).size() == 1; }));
    const Json recent = call(w, "gui.list_recent")["recent"];
    ASSERT_EQ(recent.size(), 1u);
    EXPECT_EQ(recent[0]["path"], a.toStdString());
    // gui.open_recent counts what File > Open Recent shows.
    call(w, "gui.open_recent", {{"index", 0}});
    EXPECT_TRUE(waitUntil([&] { return w.windowTitle().contains(QStringLiteral("a.nes")); }));
    call(w, "gui.close_game");

    call(w, "gui.library_unhide", {{"identities", Json::array({identityOf(b).toStdString()})}});
    EXPECT_TRUE(waitUntil([&] { return menu->findChildren<QAction*>(QStringLiteral("gui.open_recent")).size() == 2; }));
    EXPECT_EQ(call(w, "gui.list_recent")["recent"].size(), 2u) << "its entry was kept";
}

// menus-and-commands "Hiding games": the game's menu, from a right-click or
// the ⋯ button, has Hide.
TEST(GuiHiddenGames, TheGamesMenuHidesIt)
{
    const auto options = windowOptions();
    const QString roms = options.libraryRoot + QStringLiteral("/roms");
    const QString a = copyRom(roms + QStringLiteral("/alpha.nes"), 0);
    copyRom(roms + QStringLiteral("/beta.nes"), 1);
    MainWindow w(options);
    w.show();
    ASSERT_TRUE(waitUntil([&] { return listedNames(w).size() == 2; })) << listedNames(w).join(u',').toStdString();
    auto* browser = w.findChild<LibraryBrowser*>();
    ASSERT_NE(browser, nullptr);
    ASSERT_TRUE(browser->select(a));

    // The ⋯ button, for the card under the pointer.
    QToolButton* more = browser->findChild<QToolButton*>(QStringLiteral("gameMenuButton"));
    ASSERT_NE(more, nullptr);
    QMenu* menu = browser->gameMenu(a);
    ASSERT_NE(menu, nullptr);
    QAction* hide = nullptr;
    for (QAction* action : menu->actions()) {
        if (action->objectName() == QLatin1String("gui.library_hide")) hide = action;
    }
    ASSERT_NE(hide, nullptr);
    EXPECT_EQ(hide->text(), QStringLiteral("Hide"));
    hide->trigger();
    EXPECT_TRUE(waitUntil([&] { return listedNames(w) == QStringList{QStringLiteral("beta")}; }));
    EXPECT_EQ(call(w, "gui.library_hidden")["hidden"][0]["identity"], identityOf(a).toStdString());
}

// menus-and-commands "Hiding games": Settings > Library lists them, and
// Unhide brings the selected ones back.
TEST(GuiHiddenGames, SettingsListsAndUnhidesThem)
{
    const auto options = windowOptions();
    const QString roms = options.libraryRoot + QStringLiteral("/roms");
    const QString a = copyRom(roms + QStringLiteral("/alpha.nes"), 0);
    const QString b = copyRom(roms + QStringLiteral("/beta.nes"), 1);
    MainWindow w(options);
    ASSERT_TRUE(waitUntil([&] { return listedNames(w).size() == 2; })) << listedNames(w).join(u',').toStdString();
    call(w, "gui.library_hide", {{"identity", identityOf(a).toStdString()}});
    call(w, "gui.library_hide", {{"identity", identityOf(b).toStdString()}});
    call(w, "gui.show_settings", {{"page", "library"}});
    SettingsDialog* dialog = nullptr;
    ASSERT_TRUE(waitUntil([&] { return (dialog = w.findChild<SettingsDialog*>()) != nullptr; }));
    QListWidget* list = dialog->findChild<QListWidget*>(QStringLiteral("hiddenGames"));
    ASSERT_NE(list, nullptr);
    ASSERT_TRUE(waitUntil([&] { return list->count() == 2; }));
    EXPECT_EQ(list->item(0)->text(), QStringLiteral("alpha"));
    EXPECT_EQ(list->item(1)->text(), QStringLiteral("beta"));
    QPushButton* unhide = dialog->findChild<QPushButton*>(QStringLiteral("unhideGames"));
    ASSERT_NE(unhide, nullptr);
    EXPECT_FALSE(unhide->isEnabled()) << "nothing selected";

    list->item(1)->setSelected(true);
    EXPECT_TRUE(unhide->isEnabled());
    unhide->click();
    EXPECT_TRUE(waitUntil([&] { return listedNames(w) == QStringList{QStringLiteral("beta")}; }));
    EXPECT_TRUE(waitUntil([&] { return list->count() == 1 && list->item(0)->text() == QLatin1String("alpha"); }));
}
