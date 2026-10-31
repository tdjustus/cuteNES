// G020-G025 through the window: the library browser, recent games,
// Settings > Library and battery saves (spec US1, US2, FR-002-FR-005,
// FR-012, A4; contracts/menus-and-commands.md, contracts/library-layout.md).
// The game is blargg's 01-basics, which per its readme writes its status to
// $6000 and the signature DE B0 61 to $6001-$6003.

#include "dialogs/settings_dialog.hpp"
#include "gui_support.hpp"
#include "library/library_browser.hpp"
#include "mainwindow.hpp"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QDropEvent>
#include <QFile>
#include <QFuture>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMimeData>
#include <QAbstractItemView>
#include <QUrl>

#include <gtest/gtest.h>

using cutenes::api::Json;
using cutenes::api::Result;
using cutenes::app::Library;
using cutenes::app::LibraryBrowser;
using cutenes::app::MainWindow;
using cutenes::app::SettingsDialog;
using cutenes::test::waitUntil;
using cutenes::test::windowOptions;

namespace {

const QString kRom = QStringLiteral(CUTENES_TEST_ROMS "/instr_test-v5/rom_singles/01-basics.nes");

// Newer Qt takes a QPointF for a drag position.
#if QT_VERSION >= QT_VERSION_CHECK(6, 12, 0)
const QPointF kDropPoint(10, 10);
#else
const QPoint kDropPoint(10, 10);
#endif

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

// 01-basics, with the battery flag when `battery`, copied to `path`.
QString copyRom(const QString& path, bool battery = false)
{
    QFile in(kRom);
    EXPECT_TRUE(in.open(QIODevice::ReadOnly));
    QByteArray rom = in.readAll();
    if (battery) rom[6] = char(rom[6] | 0x02);
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile out(path);
    EXPECT_TRUE(out.open(QIODevice::WriteOnly));
    out.write(rom);
    return QFileInfo(path).absoluteFilePath();
}

QString romsDir(const MainWindow::Options& options)
{
    return options.libraryRoot + QStringLiteral("/roms");
}

size_t listed(MainWindow& w)
{
    return call(w, "gui.library_list")["entries"].size();
}

bool showsGame(MainWindow& w, const QString& name)
{
    return !w.isShowingLibrary() && w.windowTitle().contains(name);
}

int peek(MainWindow& w, int addr)
{
    const Json r = call(w, "core.peek", {{"addr", addr}, {"length", 1}});
    const QByteArray bytes = QByteArray::fromBase64(QByteArray::fromStdString(r["bytes"].get<std::string>()));
    return bytes.isEmpty() ? -1 : static_cast<unsigned char>(bytes[0]);
}

} // namespace

TEST(GuiLibraryWindow, StartsOnTheBrowserAndCreatesTheLibrary)
{
    const auto options = windowOptions();
    MainWindow w(options);
    EXPECT_TRUE(w.isShowingLibrary());
    EXPECT_TRUE(QFile::exists(romsDir(options) + QStringLiteral("/README.txt")));
    for (const char* folder : {"saves", "states", "screenshots", "movies", "recordings", "cheats", "debug"}) {
        EXPECT_TRUE(QDir(options.libraryRoot + QLatin1Char('/') + QLatin1String(folder)).exists()) << folder;
    }
    EXPECT_EQ(call(w, "gui.library_list")["root"], options.libraryRoot.toStdString());
}

TEST(GuiLibraryWindow, AnEmptyLibraryShowsHowToAddRoms)
{
    MainWindow w(windowOptions());
    w.show();
    auto* hint = w.browser()->findChild<QLabel*>(QStringLiteral("libraryEmpty"));
    ASSERT_NE(hint, nullptr);
    EXPECT_TRUE(waitUntil([&] { return hint->isVisible(); }));
}

TEST(GuiLibraryWindow, ListsTheRomsInTheLibrary)
{
    const auto options = windowOptions();
    copyRom(romsDir(options) + QStringLiteral("/01-basics.nes"));
    MainWindow w(options);
    ASSERT_TRUE(waitUntil([&] { return listed(w) == 1; }));
    const Json e = call(w, "gui.library_list")["entries"][0];
    EXPECT_EQ(e["name"], "01-basics");
    EXPECT_EQ(e["relativePath"], "01-basics.nes");
    EXPECT_EQ(e["mapper"], 0);
    EXPECT_TRUE(e["supported"].get<bool>());
    EXPECT_EQ(e["identity"].get<std::string>().size(), 40u);

    // FR-074: API clients see the same roms/ folder.
    const Json roms = call(w, "session.library_list")["roms"];
    ASSERT_EQ(roms.size(), 1u);
    EXPECT_EQ(roms[0], "01-basics.nes");
}

TEST(GuiLibraryWindow, ARomAddedLaterAppears)
{
    const auto options = windowOptions();
    MainWindow w(options);
    ASSERT_TRUE(waitUntil([&] { return !w.libraryIndex()->scanning(); }));
    EXPECT_EQ(listed(w), 0u);
    copyRom(romsDir(options) + QStringLiteral("/later.nes"));
    EXPECT_TRUE(waitUntil([&] { return listed(w) == 1; }, 10000)); // FR-005: the watcher rescans
}

TEST(GuiLibraryWindow, TypingFiltersAndEnterLaunches)
{
    const auto options = windowOptions();
    copyRom(romsDir(options) + QStringLiteral("/01-basics.nes"));
    copyRom(romsDir(options) + QStringLiteral("/other.nes"));
    MainWindow w(options);
    w.show();
    LibraryBrowser* b = w.browser();
    ASSERT_TRUE(waitUntil([&] { return b->visibleCount() == 2; }));

    b->setFilterText(QStringLiteral("zzz"));
    EXPECT_EQ(b->visibleCount(), 0);
    b->setFilterText(QStringLiteral("BASICS")); // case-insensitive
    ASSERT_EQ(b->visibleCount(), 1);

    ASSERT_TRUE(b->select(QFileInfo(romsDir(options) + QStringLiteral("/01-basics.nes")).absoluteFilePath()));
    QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QApplication::sendEvent(b->view(), &enter);
    EXPECT_TRUE(waitUntil([&] { return showsGame(w, QStringLiteral("01-basics.nes")); }));
    EXPECT_EQ(runState(w), "Running");
}

// The grid and the list show the same games with one selection.
TEST(GuiLibraryWindow, SwitchingToTheListKeepsTheSelection)
{
    const auto options = windowOptions();
    copyRom(romsDir(options) + QStringLiteral("/a.nes"));
    const QString b = copyRom(romsDir(options) + QStringLiteral("/b.nes"));
    MainWindow w(options);
    w.show();
    LibraryBrowser* browser = w.browser();
    ASSERT_TRUE(waitUntil([&] { return browser->visibleCount() == 2; }));
    ASSERT_TRUE(browser->select(b));
    browser->setViewLayout(LibraryBrowser::Layout::List);
    EXPECT_EQ(browser->view()->objectName(), QStringLiteral("libraryList"));
    EXPECT_EQ(browser->selectedPath(), b);
    QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QApplication::sendEvent(browser->view(), &enter);
    EXPECT_TRUE(waitUntil([&] { return showsGame(w, QStringLiteral("b.nes")); }));
}

TEST(GuiLibraryWindow, TheLibraryPausesTheGameAndEscapeReturnsToIt)
{
    MainWindow w(windowOptions());
    w.show();
    w.openRom(kRom);
    ASSERT_TRUE(waitUntil([&] { return showsGame(w, QStringLiteral("01-basics.nes")); }));

    call(w, "gui.show_library");
    EXPECT_EQ(runState(w), "Paused");
    ASSERT_TRUE(waitUntil([&] { return w.isShowingLibrary(); }));

    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(w.browser()->view(), &escape);
    EXPECT_FALSE(w.isShowingLibrary());
    EXPECT_TRUE(waitUntil([&] { return runState(w) == "Running"; }));
}

TEST(GuiLibraryWindow, AGamePausedBeforeShowingTheLibraryStaysPaused)
{
    MainWindow w(windowOptions());
    w.show();
    w.openRom(kRom);
    ASSERT_TRUE(waitUntil([&] { return showsGame(w, QStringLiteral("01-basics.nes")); }));
    call(w, "gui.toggle_pause");
    call(w, "gui.show_library");
    ASSERT_TRUE(waitUntil([&] { return w.isShowingLibrary(); }));
    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(w.browser()->view(), &escape);
    EXPECT_FALSE(w.isShowingLibrary());
    waitUntil([] { return false; }, 100);
    EXPECT_EQ(runState(w), "Paused");
}

TEST(GuiLibraryWindow, LibrarySelectFindsAGameByPathOrIdentity)
{
    const auto options = windowOptions();
    const QString rom = copyRom(romsDir(options) + QStringLiteral("/01-basics.nes"));
    MainWindow w(options);
    ASSERT_TRUE(waitUntil([&] { return listed(w) == 1; }));
    const std::string identity = call(w, "gui.library_list")["entries"][0]["identity"];

    EXPECT_EQ(call(w, "gui.library_select", {{"identity", identity}})["path"], rom.toStdString());
    EXPECT_TRUE(waitUntil([&] { return w.browser()->selectedPath() == rom; }));
    EXPECT_EQ(call(w, "gui.library_select", {{"path", rom.toStdString()}})["path"], rom.toStdString());

    const Result missing = w.session().call("gui.library_select", {{"path", "/no/such.nes"}});
    ASSERT_TRUE(missing.isError());
    EXPECT_EQ(missing.error->name, "InvalidArgument");
}

TEST(GuiLibraryWindow, OpenedGamesAreRecentNewestFirst)
{
    const auto options = windowOptions();
    QString dir;
    const auto elsewhere = windowOptions(&dir);
    const QString a = copyRom(dir + QStringLiteral("/a.nes"));
    const QString b = copyRom(dir + QStringLiteral("/b.nes"));
    MainWindow w(options);
    call(w, "gui.open_rom", {{"path", a.toStdString()}});
    call(w, "gui.open_rom", {{"path", b.toStdString()}});

    const Json recent = call(w, "gui.list_recent")["recent"];
    ASSERT_EQ(recent.size(), 2u);
    EXPECT_EQ(recent[0]["path"], b.toStdString());
    EXPECT_EQ(recent[1]["path"], a.toStdString());

    // File > Open Recent lists them, and its first item reopens b.
    QMenu* menu = w.menuBar()->findChild<QMenu*>(QStringLiteral("menuRecent"));
    ASSERT_NE(menu, nullptr);
    ASSERT_TRUE(waitUntil([&] { return menu->findChildren<QAction*>(QStringLiteral("gui.open_recent")).size() == 2; }));
    // A ROM outside the library joins the browser as a recent game.
    EXPECT_TRUE(waitUntil([&] { return listed(w) == 2; }));

    call(w, "gui.open_recent", {{"index", 1}});
    EXPECT_EQ(call(w, "gui.list_recent")["recent"][0]["path"], a.toStdString());

    call(w, "gui.clear_recent");
    EXPECT_TRUE(call(w, "gui.list_recent")["recent"].empty());
    EXPECT_TRUE(waitUntil([&] { return menu->findChildren<QAction*>(QStringLiteral("gui.open_recent")).isEmpty(); }));
    QAction* clear = menu->findChild<QAction*>(QStringLiteral("gui.clear_recent"));
    ASSERT_NE(clear, nullptr);
    EXPECT_FALSE(clear->isEnabled());
}

TEST(GuiLibraryWindow, RecentGamesSurviveARestart)
{
    const auto options = windowOptions();
    {
        MainWindow w(options);
        call(w, "gui.open_rom", {{"path", kRom.toStdString()}});
    }
    MainWindow w(options);
    const Json recent = call(w, "gui.list_recent")["recent"];
    ASSERT_EQ(recent.size(), 1u);
    EXPECT_EQ(recent[0]["path"], QFileInfo(kRom).absoluteFilePath().toStdString());
    EXPECT_TRUE(recent[0]["exists"].get<bool>());
}

TEST(GuiLibraryWindow, SettingsOpensAtTheLibraryPage)
{
    const auto options = windowOptions();
    MainWindow w(options);
    EXPECT_EQ(call(w, "gui.show_settings")["page"], "library");
    SettingsDialog* dialog = nullptr;
    ASSERT_TRUE(waitUntil([&] {
        dialog = w.findChild<SettingsDialog*>(QStringLiteral("settingsDialog"));
        return dialog && dialog->isVisible();
    }));
    EXPECT_EQ(dialog->currentPage(), QStringLiteral("library"));
    auto* root = dialog->findChild<QLabel*>(QStringLiteral("libraryRoot"));
    ASSERT_NE(root, nullptr);
    EXPECT_TRUE(root->text().contains(QDir::toNativeSeparators(options.libraryRoot)));

    const Json settings = call(w, "gui.get_settings", {{"group", "library"}});
    EXPECT_EQ(settings["values"]["library"]["root"], options.libraryRoot.toStdString());
}

TEST(GuiLibraryWindow, SetSettingMovesTheLibraryLocation)
{
    const auto options = windowOptions();
    QString dir;
    windowOptions(&dir);
    const QString target = dir + QStringLiteral("/elsewhere");
    MainWindow w(options);
    const Json r = call(w, "gui.set_setting", {{"key", "library.root"}, {"value", target.toStdString()}});
    EXPECT_EQ(r["value"], target.toStdString());
    EXPECT_TRUE(QFile::exists(target + QStringLiteral("/roms/README.txt")));
    EXPECT_TRUE(waitUntil([&] { return w.library().root() == target; }));
    EXPECT_EQ(call(w, "gui.library_list")["root"], target.toStdString());
    EXPECT_EQ(w.settings().libraryRoot(), target);

    for (const char* bad : {"", "relative/path"}) {
        const Result rejected = w.session().call("gui.set_setting", {{"key", "library.root"}, {"value", bad}});
        ASSERT_TRUE(rejected.isError()) << bad;
        EXPECT_EQ(rejected.error->name, "InvalidArgument") << bad;
    }
}

TEST(GuiLibraryWindow, RelocatingWithContentsBringsTheRomsAlong)
{
    const auto options = windowOptions();
    QString dir;
    windowOptions(&dir);
    const QString target = dir + QStringLiteral("/moved");
    copyRom(romsDir(options) + QStringLiteral("/01-basics.nes"));
    MainWindow w(options);
    // The future finishes on the GUI thread: wait with events running.
    const QFuture<Result> moved = w.relocateLibrary(target, true);
    ASSERT_TRUE(waitUntil([&] { return moved.isFinished(); }));
    const Result r = moved.result();
    ASSERT_FALSE(r.isError()) << r.error->message;
    EXPECT_TRUE(QFile::exists(target + QStringLiteral("/roms/01-basics.nes")));
    ASSERT_TRUE(waitUntil([&] { return w.library().root() == target && listed(w) == 1; }));
    EXPECT_EQ(call(w, "gui.library_list")["entries"][0]["relativePath"], "01-basics.nes");
}

TEST(GuiLibraryWindow, OpenLibraryFolderShowsTheRoot)
{
    const auto options = windowOptions();
    MainWindow w(options);
    QString opened;
    QObject::connect(&w, &MainWindow::libraryFolderOpened, [&](const QString& path) { opened = path; });
    call(w, "gui.open_library_folder");
    ASSERT_TRUE(waitUntil([&] { return !opened.isEmpty(); }));
    EXPECT_EQ(opened, options.libraryRoot);
}

TEST(GuiLibraryWindow, DroppingARomOpensIt)
{
    MainWindow w(windowOptions());
    w.show();
    QMimeData data;
    data.setUrls({QUrl::fromLocalFile(kRom)});
    QDragEnterEvent enter(kDropPoint, Qt::CopyAction, &data, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&w, &enter);
    EXPECT_TRUE(enter.isAccepted());
    QDropEvent drop(QPointF(10, 10), Qt::CopyAction, &data, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&w, &drop);
    EXPECT_TRUE(waitUntil([&] { return showsGame(w, QStringLiteral("01-basics.nes")); }));
}

TEST(GuiLibraryWindow, OnlyNesFilesAreAcceptedForDropping)
{
    MainWindow w(windowOptions());
    QMimeData data;
    data.setUrls({QUrl::fromLocalFile(QStringLiteral("/tmp/notes.txt"))});
    QDragEnterEvent enter(kDropPoint, Qt::CopyAction, &data, Qt::LeftButton, Qt::NoModifier);
    enter.ignore();
    QApplication::sendEvent(&w, &enter);
    EXPECT_FALSE(enter.isAccepted());
}

// A4: the save is found by the game, not the file name, and written when the
// window closes.
TEST(GuiLibraryWindow, ABatterySaveSurvivesARestartAndARename)
{
    const auto options = windowOptions();
    const QString first = copyRom(romsDir(options) + QStringLiteral("/Quest.nes"), true);
    std::string savePath;
    {
        MainWindow w(options);
        const Json r = call(w, "gui.open_rom", {{"path", first.toStdString()}});
        savePath = r["batterySave"].get<std::string>();
        ASSERT_FALSE(savePath.empty());
        EXPECT_FALSE(r.contains("warning")) << r["warning"];
        ASSERT_TRUE(waitUntil([&] { return peek(w, 0x6001) == 0xDE; }));
        call(w, "core.pause");
        // A byte the test ROM never touches stands for the game's progress.
        call(w, "core.write_memory", {{"addr", 0x7F00}, {"bytes", "Wg=="}}); // 0x5A
        w.close(); // FR-012: written before quitting
    }
    ASSERT_TRUE(QFile::exists(QString::fromStdString(savePath)));

    const QString renamed = romsDir(options) + QStringLiteral("/Quest (renamed).nes");
    ASSERT_TRUE(QFile::rename(first, renamed));
    MainWindow w(options);
    const Json r = call(w, "gui.open_rom", {{"path", renamed.toStdString()}});
    EXPECT_EQ(r["batterySave"], savePath);
    call(w, "core.pause");
    EXPECT_EQ(peek(w, 0x7F00), 0x5A);
}

// Added ROM folders (Settings > Library): scanned in place and watched,
// removable, and kept across restarts. The library's own roms/ folder is
// always scanned and cannot be removed.
TEST(GuiLibraryWindow, AnAddedRomFolderIsScannedInPlace)
{
    const auto options = windowOptions();
    QString dir;
    windowOptions(&dir);
    const QString mine = dir + QStringLiteral("/My ROMs");
    const QString rom = copyRom(mine + QStringLiteral("/Mine.nes"));
    MainWindow w(options);

    const Json added = call(w, "gui.add_rom_folder", {{"path", mine.toStdString()}});
    ASSERT_EQ(added["romFolders"].size(), 1u);
    EXPECT_EQ(added["romFolders"][0], mine.toStdString());
    ASSERT_TRUE(waitUntil([&] { return listed(w) == 1; }));
    const Json e = call(w, "gui.library_list")["entries"][0];
    EXPECT_EQ(e["path"], rom.toStdString());
    EXPECT_EQ(e["relativePath"], "");
    EXPECT_EQ(QDir(mine).entryList(QDir::AllEntries | QDir::NoDotAndDotDot), QStringList{QStringLiteral("Mine.nes")});

    const Json settings = call(w, "gui.get_settings", {{"group", "library"}})["values"]["library"];
    EXPECT_EQ(settings["roms"], romsDir(options).toStdString());
    EXPECT_EQ(settings["romFolders"], Json::array({mine.toStdString()}));

    // A ROM copied there later appears (FR-005).
    copyRom(mine + QStringLiteral("/sub/Later.nes"));
    EXPECT_TRUE(waitUntil([&] { return listed(w) == 2; }, 10000));
}

TEST(GuiLibraryWindow, ARomFolderCanBeRemovedAndIsRemembered)
{
    const auto options = windowOptions();
    QString dir;
    windowOptions(&dir);
    const QString mine = dir + QStringLiteral("/mine");
    copyRom(mine + QStringLiteral("/Mine.nes"));
    {
        MainWindow w(options);
        call(w, "gui.add_rom_folder", {{"path", mine.toStdString()}});
    }
    MainWindow w(options);
    ASSERT_TRUE(waitUntil([&] { return listed(w) == 1; })) << "remembered across a restart";
    const Json removed = call(w, "gui.remove_rom_folder", {{"path", mine.toStdString()}});
    EXPECT_TRUE(removed["romFolders"].empty());
    EXPECT_TRUE(waitUntil([&] { return listed(w) == 0; }));
    EXPECT_TRUE(QFile::exists(mine + QStringLiteral("/Mine.nes"))) << "removing a folder deletes nothing";
}

TEST(GuiLibraryWindow, TheLibraryRomsFolderCannotBeRemoved)
{
    const auto options = windowOptions();
    MainWindow w(options);
    const Result r = w.session().call("gui.remove_rom_folder", {{"path", romsDir(options).toStdString()}});
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(r.error->name, "InvalidArgument");
    EXPECT_TRUE(QDir(romsDir(options)).exists());
}

TEST(GuiLibraryWindow, AddingAMissingDuplicateOrNestedFolderFails)
{
    const auto options = windowOptions();
    QString dir;
    windowOptions(&dir);
    const QString mine = dir + QStringLiteral("/mine");
    QDir().mkpath(mine + QStringLiteral("/inner"));
    MainWindow w(options);
    call(w, "gui.add_rom_folder", {{"path", mine.toStdString()}});
    for (const QString& bad : {dir + QStringLiteral("/missing"), mine, mine + QStringLiteral("/inner"),
             romsDir(options), QStringLiteral("relative")}) {
        const Result r = w.session().call("gui.add_rom_folder", {{"path", bad.toStdString()}});
        ASSERT_TRUE(r.isError()) << bad.toStdString();
        EXPECT_EQ(r.error->name, "InvalidArgument") << bad.toStdString();
    }
    EXPECT_EQ(w.settings().romFolders(), QStringList{mine});
}
