// G030-G031: save-state slots (spec US4-1 to US4-3, FR-033, SC-007,
// data-model "StateSlot", contracts/library-layout.md). A state is a full
// machine snapshot, so running the same frames after loading it must give
// the same frame hash as running them after saving it.

#include "gui_support.hpp"
#include "mainwindow.hpp"
#include "persistence/state_slots.hpp"

#include <cutenes/qt/audio_output.hpp>
#include <cutenes/qt/emulation_view.hpp>
#include <cutenes/qt/media_runtime.hpp>

#include <QAction>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QTemporaryDir>

#include <gtest/gtest.h>

using cutenes::api::Json;
using cutenes::api::Result;
using cutenes::app::MainWindow;
using cutenes::app::StateSlots;
using cutenes::test::waitUntil;
using cutenes::test::windowOptions;

namespace {

const QString kRom = QStringLiteral(CUTENES_TEST_ROMS "/instr_test-v5/rom_singles/01-basics.nes");
const QString kOtherRom = QStringLiteral(CUTENES_TEST_ROMS "/instr_test-v5/rom_singles/02-implied.nes");
// A different board (MMC3): the core tells cartridges apart by mapper and
// sizes (001 save-state-format.md "cartridgeId"; same-board games, #92).
const QString kOtherBoard = QStringLiteral(CUTENES_TEST_ROMS "/mmc3_test/1-clocking.nes");

Json call(MainWindow& w, const char* name, const Json& args = Json::object())
{
    const Result r = w.session().call(name, args);
    EXPECT_FALSE(r.isError()) << name << ": " << (r.isError() ? r.error->message : "");
    return r.value;
}

Result attempt(MainWindow& w, const char* name, const Json& args = Json::object())
{
    return w.session().call(name, args);
}

// Opens a game and pauses it, so frames run only when a test says so.
void openPaused(MainWindow& w, const QString& rom = kRom)
{
    call(w, "gui.open_rom", {{"path", rom.toStdString()}});
    call(w, "core.pause");
    call(w, "core.run_frames", {{"frames", 30}});
}

std::string hashAfter(MainWindow& w, int frames)
{
    return call(w, "core.run_frames", {{"frames", frames}})["frameHash"].get<std::string>();
}

std::uint64_t frame(MainWindow& w)
{
    return call(w, "core.counters")["frame"].get<std::uint64_t>();
}

QAction* slotAction(MainWindow& w, const char* command, int slot)
{
    for (QAction* a : w.menuBar()->findChildren<QAction*>()) {
        if (a->property("command").toString() == QLatin1String(command)
            && a->property("commandArgs").toMap().value(QStringLiteral("slot")).toInt() == slot) {
            return a;
        }
    }
    return nullptr;
}

} // namespace

TEST(GuiStates, ASlotLoadsBackToTheSameFrameAndHash)
{
    MainWindow w(windowOptions());
    openPaused(w);
    const Json saved = call(w, "gui.save_state", {{"slot", 3}});
    EXPECT_EQ(saved["slot"], 3);
    EXPECT_FALSE(saved["empty"].get<bool>());
    const std::uint64_t savedFrame = frame(w);
    const std::string expected = hashAfter(w, 120);

    call(w, "core.run_frames", {{"frames", 45}});
    call(w, "gui.load_state", {{"slot", 3}});
    EXPECT_EQ(frame(w), savedFrame);
    EXPECT_EQ(hashAfter(w, 120), expected) << "SC-007";
}

TEST(GuiStates, ASlotIsAStateAndAThumbnailInTheGamesStatesFolder)
{
    MainWindow w(windowOptions());
    openPaused(w);
    const Json saved = call(w, "gui.save_state", {{"slot", 10}});
    const QString state = QString::fromStdString(saved["state"].get<std::string>());
    EXPECT_TRUE(state.endsWith(QStringLiteral("/slot-10.state"))) << state.toStdString();
    // states/<display name> [<id8>]/ (contracts/library-layout.md)
    const QFileInfo folder(QFileInfo(state).path());
    EXPECT_EQ(folder.dir().absolutePath(), QDir(w.library().folder(cutenes::app::Library::Folder::States)).absolutePath());
    EXPECT_TRUE(folder.fileName().startsWith(QStringLiteral("01-basics [")));
    EXPECT_EQ(QFileInfo(state).size(), saved["size"].get<qint64>());

    const QImage thumbnail(QString::fromStdString(saved["thumbnail"].get<std::string>()));
    EXPECT_EQ(thumbnail.size(), QSize(256, 240));
    // The picture is the screen when the state was saved.
    const Json screen = call(w, "core.screen", {{"format", "raw"}});
    QByteArray raw;
    for (bool eof = false; !eof;) {
        const Json part = call(w, "session.blob_read", {{"blobId", screen["blobId"]}, {"offset", raw.size()}, {"length", 65536}});
        raw += QByteArray::fromBase64(QByteArray::fromStdString(part["bytes"].get<std::string>()));
        eof = part["eof"].get<bool>();
    }
    ASSERT_EQ(raw.size(), 256 * 240 * 4);
    const QImage expected(reinterpret_cast<const uchar*>(raw.constData()), 256, 240, QImage::Format_RGB32);
    EXPECT_EQ(thumbnail.convertToFormat(QImage::Format_RGB888), expected.convertToFormat(QImage::Format_RGB888));
}

TEST(GuiStates, ListSlotsShowsTimesAndEmptySlots)
{
    MainWindow w(windowOptions());
    openPaused(w);
    const QDateTime before = QDateTime::currentDateTimeUtc().addSecs(-2);
    call(w, "gui.save_state", {{"slot", 2}});
    const Json list = call(w, "gui.list_slots");
    ASSERT_EQ(list["slots"].size(), 10u);
    for (const Json& slot : list["slots"]) { // list outlives the loop
        const bool filled = slot["slot"] == 2;
        EXPECT_EQ(slot["empty"].get<bool>(), !filled) << slot.dump();
        if (filled) {
            const QDateTime at = QDateTime::fromString(QString::fromStdString(slot["savedAt"].get<std::string>()), Qt::ISODate);
            EXPECT_GE(at, before);
            EXPECT_TRUE(slot.contains("thumbnail"));
        }
    }
    EXPECT_EQ(list["selectedSlot"], 1) << "a game's selected slot starts at 1";
}

TEST(GuiStates, QuickSaveAndLoadUseTheSelectedSlotRememberedPerGame)
{
    const auto options = windowOptions();
    {
        MainWindow w(options);
        openPaused(w);
        call(w, "gui.select_slot", {{"slot", 7}});
        EXPECT_EQ(call(w, "gui.quick_save")["slot"], 7);
        const std::string expected = hashAfter(w, 60);
        EXPECT_EQ(call(w, "gui.quick_load")["slot"], 7);
        EXPECT_EQ(hashAfter(w, 60), expected);

        // Another game has its own selection.
        openPaused(w, kOtherRom);
        EXPECT_EQ(call(w, "gui.list_slots")["selectedSlot"], 1);
        EXPECT_TRUE(call(w, "gui.list_slots")["slots"][6]["empty"].get<bool>()) << "and its own slots";
    }
    MainWindow again(options);
    openPaused(again);
    EXPECT_EQ(call(again, "gui.list_slots")["selectedSlot"], 7);
    EXPECT_FALSE(call(again, "gui.list_slots")["slots"][6]["empty"].get<bool>());
}

TEST(GuiStates, UndoLoadStateGoesBackToWhatTheLoadReplaced)
{
    MainWindow w(windowOptions());
    openPaused(w);
    call(w, "gui.save_state", {{"slot", 1}});
    call(w, "core.run_frames", {{"frames", 90}});
    // Where play had got to, kept in the session's own memory slot.
    call(w, "session.save_slot", {{"name", "before-load"}});
    const std::uint64_t playFrame = frame(w);

    call(w, "gui.load_state", {{"slot", 1}});
    EXPECT_NE(frame(w), playFrame);
    call(w, "gui.undo_load_state");
    EXPECT_EQ(frame(w), playFrame);
    const std::string undone = hashAfter(w, 60);
    call(w, "session.load_slot", {{"name", "before-load"}});
    EXPECT_EQ(hashAfter(w, 60), undone);

    const Result again = attempt(w, "gui.undo_load_state");
    ASSERT_TRUE(again.isError()) << "one level of undo";
    EXPECT_EQ(again.error->name, "NotFound");
}

TEST(GuiStates, LoadingAnEmptySlotFailsAndChangesNothing)
{
    MainWindow w(windowOptions());
    openPaused(w);
    const std::uint64_t at = frame(w);
    const Result r = attempt(w, "gui.load_state", {{"slot", 4}});
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(r.error->name, "NotFound");
    EXPECT_EQ(frame(w), at);
    EXPECT_TRUE(attempt(w, "gui.undo_load_state").isError()) << "a failed load leaves nothing to undo";
}

TEST(GuiStates, AnotherGamesStateIsRefusedAndLeavesTheGameAlone)
{
    MainWindow w(windowOptions());
    QTemporaryDir dir;
    const QString file = dir.filePath(QStringLiteral("other.state"));
    openPaused(w, kOtherBoard);
    call(w, "gui.save_state_file", {{"path", file.toStdString()}});

    openPaused(w);
    const std::uint64_t at = frame(w);
    const Result r = attempt(w, "gui.load_state_file", {{"path", file.toStdString()}});
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(r.error->name, "LoadStateError");
    EXPECT_EQ(r.error->data.value("error", ""), "WrongCartridge");
    EXPECT_EQ(frame(w), at);
}

TEST(GuiStates, StatesRoundTripThroughAFile)
{
    MainWindow w(windowOptions());
    QTemporaryDir dir;
    const QString file = dir.filePath(QStringLiteral("anywhere.state"));
    openPaused(w);
    call(w, "gui.save_state_file", {{"path", file.toStdString()}});
    EXPECT_TRUE(QFileInfo(file).isFile());
    const std::string expected = hashAfter(w, 60);
    call(w, "gui.load_state_file", {{"path", file.toStdString()}});
    EXPECT_EQ(hashAfter(w, 60), expected);
    call(w, "gui.undo_load_state"); // a file load can be undone too
}

TEST(GuiStates, SlotCommandsNeedAGame)
{
    MainWindow w(windowOptions());
    for (const char* name : {"gui.save_state", "gui.load_state"}) {
        const Result r = attempt(w, name, {{"slot", 1}});
        ASSERT_TRUE(r.isError()) << name;
        EXPECT_EQ(r.error->name, "NoCartridge") << name;
    }
    EXPECT_TRUE(attempt(w, "gui.quick_save").isError());
    EXPECT_FALSE(attempt(w, "gui.select_slot", {{"slot", 2}}).isError()) << "the contract's Game column is blank";
    EXPECT_TRUE(attempt(w, "gui.save_state", {{"slot", 11}}).isError()) << "slots are 1-10";
}

TEST(GuiStates, SavingConfirmsWithAMessage)
{
    MainWindow w(windowOptions());
    QStringList messages;
    QObject::connect(&w, &MainWindow::messageShown, [&](const QString& text) { messages << text; });
    openPaused(w);
    call(w, "gui.save_state", {{"slot", 5}});
    EXPECT_TRUE(waitUntil([&] { return messages.contains(QStringLiteral("State saved to slot 5")); }));
}

TEST(GuiStates, TheSlotMenusShowEachSlotsTimeAndThumbnail)
{
    MainWindow w(windowOptions());
    openPaused(w);
    call(w, "gui.save_state", {{"slot", 2}});
    call(w, "gui.select_slot", {{"slot", 2}});
    auto* load = qobject_cast<QMenu*>(slotAction(w, "gui.load_state", 2)->parent());
    ASSERT_NE(load, nullptr);
    emit load->aboutToShow();
    EXPECT_TRUE(slotAction(w, "gui.load_state", 1)->text().contains(QStringLiteral("Empty")));
    EXPECT_FALSE(slotAction(w, "gui.load_state", 2)->text().contains(QStringLiteral("Empty")));
    EXPECT_FALSE(slotAction(w, "gui.load_state", 2)->icon().isNull());
    EXPECT_TRUE(slotAction(w, "gui.select_slot", 2)->isChecked());
    EXPECT_FALSE(slotAction(w, "gui.select_slot", 1)->isChecked());
}

TEST(GuiStates, TheLibraryCountsAGamesStates)
{
    MainWindow w(windowOptions());
    QDir(w.library().folder(cutenes::app::Library::Folder::Roms)).mkpath(QStringLiteral("."));
    const QString rom = QDir(w.library().folder(cutenes::app::Library::Folder::Roms)).filePath(QStringLiteral("basics.nes"));
    ASSERT_TRUE(QFile::copy(kRom, rom));
    openPaused(w, rom);
    call(w, "gui.save_state", {{"slot", 1}});
    call(w, "gui.save_state", {{"slot", 2}});
    call(w, "gui.close_game");
    EXPECT_TRUE(waitUntil([&] {
        const Json list = call(w, "gui.library_list");
        for (const Json& e : list["entries"]) {
            if (e["stateCount"] == 2) return true;
        }
        return false;
    })) << "the library index refreshes after the close";
}

// A bug the maintainer found: the first message added a status bar, which
// took its height from the game view for good. Messages float over the
// game and never resize it.
TEST(GuiStates, AMessageNeverResizesTheGameView)
{
    MainWindow w(windowOptions());
    w.show();
    QStringList messages;
    QObject::connect(&w, &MainWindow::messageShown, [&](const QString& text) { messages << text; });
    openPaused(w);
    ASSERT_TRUE(waitUntil([&] { return w.view()->isVisible(); }));
    const QSize window = w.size();
    const QSize view = w.view()->size();
    call(w, "gui.save_state", {{"slot", 1}});
    ASSERT_TRUE(waitUntil([&] { return !messages.isEmpty(); }));
    QCoreApplication::processEvents();
    EXPECT_EQ(w.view()->size(), view);
    EXPECT_EQ(w.size(), window);
    auto* osd = w.findChild<QLabel*>(QStringLiteral("osdMessage"));
    ASSERT_NE(osd, nullptr);
    EXPECT_TRUE(osd->isVisible());
    EXPECT_EQ(osd->text(), QStringLiteral("State saved to slot 1"));
    EXPECT_TRUE(waitUntil([&] { return !osd->isVisible(); }, 6000)) << "it goes away by itself";
    EXPECT_EQ(w.view()->size(), view);
}

// The maintainer's choice: a load leaves the game paused, with PAUSED over
// it (the core does the same), and it plays on, with sound, once resumed.
// The first build froze on the loaded picture, as nothing said it was
// paused, and the maintainer heard no sound after a load.
TEST(GuiStates, ALoadedGameWaitsPausedAndPlaysOnWithSound)
{
    const cutenes::qt::MediaRuntime media; // SDL audio (the dummy driver)
    MainWindow w(windowOptions());
    w.show();
    call(w, "gui.open_rom", {{"path", kRom.toStdString()}});
    ASSERT_TRUE(waitUntil([&] { return frame(w) > 30; }));
    call(w, "gui.save_state", {{"slot", 1}});
    ASSERT_TRUE(waitUntil([&] { return frame(w) > 60; }));
    call(w, "gui.load_state", {{"slot", 1}});
    EXPECT_EQ(call(w, "core.run_state")["runState"], "Paused");
    auto* paused = w.findChild<QLabel*>(QStringLiteral("statusOverlay"));
    ASSERT_NE(paused, nullptr);
    EXPECT_TRUE(waitUntil([&] { return paused->isVisible(); }));

    call(w, "gui.toggle_pause");
    const std::uint64_t loaded = frame(w);
    EXPECT_TRUE(waitUntil([&] { return frame(w) > loaded + 30; })) << "frames run after the resume";
    const std::uint64_t shown = w.view()->shownFrameIndex();
    EXPECT_TRUE(waitUntil([&] { return w.view()->shownFrameIndex() > shown + 10; })) << "and the view shows them";
    if (w.audio()->isOpen()) {
        EXPECT_TRUE(waitUntil([&] { return w.audio()->stats().queuedMs > 0; })) << "and sound reaches the device";
    }
    EXPECT_FALSE(paused->isVisible());
}

// The maintainer's requests: saving from the menus asks for a name (the
// time by default) and warns before replacing a state; automation saves
// without asking.
TEST(GuiStates, SavingFromTheMenuAsksForANameAndWarnsBeforeReplacing)
{
    MainWindow w(windowOptions());
    w.show();
    call(w, "gui.open_rom", {{"path", kRom.toStdString()}});
    ASSERT_TRUE(waitUntil([&] { return slotAction(w, "gui.save_state", 4)->isEnabled(); }));

    slotAction(w, "gui.save_state", 4)->trigger();
    QDialog* dialog = nullptr;
    ASSERT_TRUE(waitUntil([&] { return (dialog = w.findChild<QDialog*>(QStringLiteral("saveStateDialog"))) != nullptr; }));
    EXPECT_TRUE(waitUntil([&] { return call(w, "core.run_state")["runState"] == "Paused"; }))
        << "the state is the moment the key was pressed";
    auto* name = dialog->findChild<QLineEdit*>(QStringLiteral("stateName"));
    ASSERT_NE(name, nullptr);
    EXPECT_FALSE(name->text().isEmpty()) << "a default name";
    EXPECT_EQ(dialog->findChild<QLabel*>(QStringLiteral("replaceWarning")), nullptr) << "slot 4 is empty";
    name->setText(QStringLiteral("Before the boss"));
    dialog->accept();
    ASSERT_TRUE(waitUntil([&] { return !call(w, "gui.list_slots")["slots"][3]["empty"].get<bool>(); }));
    EXPECT_EQ(call(w, "gui.list_slots")["slots"][3]["name"], "Before the boss");
    EXPECT_TRUE(waitUntil([&] { return call(w, "core.run_state")["runState"] == "Running"; })) << "play goes on";

    // Again into the same slot: a warning, and Cancel keeps the old state.
    ASSERT_TRUE(waitUntil([&] { return !w.findChild<QDialog*>(QStringLiteral("saveStateDialog")); }));
    slotAction(w, "gui.save_state", 4)->trigger();
    ASSERT_TRUE(waitUntil([&] { return (dialog = w.findChild<QDialog*>(QStringLiteral("saveStateDialog"))) != nullptr; }));
    auto* warning = dialog->findChild<QLabel*>(QStringLiteral("replaceWarning"));
    ASSERT_NE(warning, nullptr);
    EXPECT_TRUE(warning->text().contains(QStringLiteral("Before the boss")));
    dialog->reject();
    ASSERT_TRUE(waitUntil([&] { return call(w, "core.run_state")["runState"] == "Running"; }));
    EXPECT_EQ(call(w, "gui.list_slots")["slots"][3]["name"], "Before the boss");

    emit qobject_cast<QMenu*>(slotAction(w, "gui.load_state", 4)->parent())->aboutToShow();
    EXPECT_EQ(slotAction(w, "gui.load_state", 4)->text(), QStringLiteral("Slot 4 — Before the boss"));
}

TEST(GuiStates, AutomationSavesWithADefaultNameAndNoDialog)
{
    MainWindow w(windowOptions());
    openPaused(w);
    const Json saved = call(w, "gui.save_state", {{"slot", 1}});
    EXPECT_FALSE(saved["name"].get<std::string>().empty());
    call(w, "gui.save_state", {{"slot", 1}, {"name", "Mine"}});
    EXPECT_EQ(call(w, "gui.list_slots")["slots"][0]["name"], "Mine");
    EXPECT_EQ(w.findChild<QDialog*>(QStringLiteral("saveStateDialog")), nullptr);
}

// States belong to the ROM's hash (R7), not its file name.
TEST(GuiStates, ARenamedRomKeepsItsStates)
{
    MainWindow w(windowOptions());
    QTemporaryDir dir;
    const QString first = dir.filePath(QStringLiteral("first name.nes"));
    const QString second = dir.filePath(QStringLiteral("another name.nes"));
    ASSERT_TRUE(QFile::copy(kRom, first));
    openPaused(w, first);
    call(w, "gui.save_state", {{"slot", 6}, {"name", "Kept"}});
    call(w, "gui.close_game");
    ASSERT_TRUE(QFile::rename(first, second));
    openPaused(w, second);
    EXPECT_EQ(call(w, "gui.list_slots")["slots"][5]["name"], "Kept");
    // A different ROM under the old name has none of them.
    ASSERT_TRUE(QFile::copy(kOtherRom, first));
    openPaused(w, first);
    EXPECT_TRUE(call(w, "gui.list_slots")["slots"][5]["empty"].get<bool>());
}

// The maintainer's request: a paused game says so over the picture.
TEST(GuiStates, APausedGameShowsPaused)
{
    MainWindow w(windowOptions());
    w.show();
    call(w, "gui.open_rom", {{"path", kRom.toStdString()}});
    ASSERT_TRUE(waitUntil([&] { return w.view()->isVisible(); }));
    auto* paused = w.findChild<QLabel*>(QStringLiteral("statusOverlay"));
    ASSERT_NE(paused, nullptr);
    EXPECT_FALSE(paused->isVisible());
    call(w, "gui.toggle_pause");
    ASSERT_TRUE(waitUntil([&] { return paused->isVisible(); }));
    EXPECT_EQ(paused->text(), QStringLiteral("PAUSED"));
    EXPECT_EQ(paused->parentWidget(), w.view()) << "over the picture, so it goes with the game page";
    EXPECT_EQ(paused->geometry().center(), w.view()->rect().center());
    call(w, "gui.toggle_pause");
    EXPECT_TRUE(waitUntil([&] { return !paused->isVisible(); }));
}
