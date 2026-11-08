// The pause menu (spec US4-8, US4-9, FR-038; contract "Pause menu", #235).
// Expectations, from the maintainer's request and the contract:
// - pausing with Pause shows Resume, Save, Battery Save (a game with
//   battery RAM only) and Quit; any other pause shows only Resume;
// - Save is Quick Save (it asks for a name, then saves the selected slot);
// - Battery Save writes the save at once, whatever the write interval;
// - Quit asks first, and a client's gui.close_game never does;
// - showing the menu runs no frame.
//
// The battery game is blargg's 01-basics with the header's battery bit set:
// per its readme it writes the signature DE B0 61 to $6001-$6003.

#include "gui_support.hpp"
#include "mainwindow.hpp"

#include <QAbstractButton>
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QKeyEvent>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>

#include <gtest/gtest.h>

#include <chrono>

using cutenes::api::Json;
using cutenes::api::Result;
using cutenes::app::MainWindow;
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

std::string runState(MainWindow& w)
{
    return call(w, "core.run_state")["runState"].get<std::string>();
}

std::string pauseMenu(MainWindow& w)
{
    return call(w, "gui.state")["pauseMenu"].get<std::string>();
}

std::uint64_t frame(MainWindow& w)
{
    return call(w, "core.run_state")["frame"].get<std::uint64_t>();
}

QByteArray readAll(const QString& path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

QString batteryRom(const QString& folder)
{
    QByteArray rom = readAll(kRom);
    rom[6] = char(rom[6] | 0x02); // battery-backed PRG-RAM
    const QString path = folder + QStringLiteral("/battery-basics.nes");
    QFile f(path);
    EXPECT_TRUE(f.open(QIODevice::WriteOnly) && f.write(rom) == rom.size());
    return path;
}

QWidget* menu(MainWindow& w)
{
    return w.findChild<QWidget*>(QStringLiteral("pauseMenu"));
}

QAbstractButton* button(MainWindow& w, const char* name)
{
    return w.findChild<QAbstractButton*>(QLatin1String(name));
}

bool shown(MainWindow& w, const char* name)
{
    QAbstractButton* b = button(w, name);
    return b && b->isVisible();
}

// Opens a game, lets it run, and pauses it with Pause.
void openAndPause(MainWindow& w, const QString& rom = kRom)
{
    call(w, "gui.open_rom", {{"path", rom.toStdString()}});
    ASSERT_TRUE(waitUntil([&] { return frame(w) > 30; }));
    call(w, "gui.toggle_pause");
    ASSERT_TRUE(waitUntil([&] { return menu(w) && menu(w)->isVisible(); }));
}

} // namespace

TEST(GuiPauseMenu, PausingWithPauseShowsTheFullMenuAndRunsNoFrame)
{
    MainWindow w(windowOptions());
    w.show();
    openAndPause(w);
    EXPECT_EQ(pauseMenu(w), "full");
    EXPECT_TRUE(shown(w, "pauseResume"));
    EXPECT_TRUE(shown(w, "pauseSave"));
    EXPECT_TRUE(shown(w, "pauseQuit"));
    EXPECT_FALSE(shown(w, "pauseBatterySave")) << "01-basics as released has no battery";
    auto* paused = w.findChild<QLabel*>(QStringLiteral("pausedOverlay"));
    ASSERT_NE(paused, nullptr);
    EXPECT_TRUE(paused->isVisible());

    const std::uint64_t before = frame(w);
    waitUntil([] { return false; }, 300);
    EXPECT_EQ(frame(w), before) << "a frame ran while the menu showed";

    button(w, "pauseResume")->click();
    EXPECT_TRUE(waitUntil([&] { return runState(w) == "Running"; }));
    EXPECT_TRUE(waitUntil([&] { return !menu(w)->isVisible(); }));
    EXPECT_EQ(pauseMenu(w), "none");
}

TEST(GuiPauseMenu, OtherPausesShowOnlyResume)
{
    MainWindow w(windowOptions());
    w.show();
    call(w, "gui.open_rom", {{"path", kRom.toStdString()}});
    ASSERT_TRUE(waitUntil([&] { return frame(w) > 30; }));

    call(w, "core.pause"); // automation
    ASSERT_TRUE(waitUntil([&] { return menu(w) && menu(w)->isVisible(); }));
    EXPECT_EQ(pauseMenu(w), "resume");
    EXPECT_TRUE(shown(w, "pauseResume"));
    EXPECT_FALSE(shown(w, "pauseSave"));
    EXPECT_FALSE(shown(w, "pauseBatterySave"));
    EXPECT_FALSE(shown(w, "pauseQuit"));
    button(w, "pauseResume")->click();
    EXPECT_TRUE(waitUntil([&] { return runState(w) == "Running"; }));

    // Frame Advance leaves the picture clear to study, even after Pause.
    call(w, "gui.toggle_pause");
    ASSERT_TRUE(waitUntil([&] { return shown(w, "pauseSave"); }));
    call(w, "gui.frame_advance");
    EXPECT_EQ(pauseMenu(w), "resume");
    EXPECT_TRUE(waitUntil([&] { return !shown(w, "pauseSave") && shown(w, "pauseResume"); }));
}

TEST(GuiPauseMenu, SaveDoesWhatQuickSaveDoes)
{
    MainWindow w(windowOptions());
    w.show();
    call(w, "gui.set_setting", {{"key", "emulation.pauseWhenInactive"}, {"value", false}});
    openAndPause(w);
    call(w, "gui.select_slot", {{"slot", 4}});
    button(w, "pauseSave")->click();
    QDialog* dialog = nullptr;
    ASSERT_TRUE(waitUntil([&] { return (dialog = w.findChild<QDialog*>(QStringLiteral("saveStateDialog"))) != nullptr; }))
        << "Quick Save asks for a name";
    dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save)->click();
    EXPECT_TRUE(waitUntil([&] {
        const Json list = call(w, "gui.list_slots")["slots"];
        return list.size() == 10 && !list[3].value("empty", true);
    })) << "the selected slot holds the state";
    EXPECT_EQ(runState(w), "Paused") << "the game waits, as it did";
    EXPECT_EQ(pauseMenu(w), "full");
}

TEST(GuiPauseMenu, BatterySaveShowsForABatteryGameAndWritesAtOnce)
{
    QString folder;
    MainWindow::Options options = windowOptions(&folder);
    options.batteryInterval = std::chrono::hours(1); // only Battery Save writes
    MainWindow w(options);
    w.show();
    const QString rom = batteryRom(folder);
    const Json opened = call(w, "gui.open_rom", {{"path", rom.toStdString()}});
    const QString save = QString::fromStdString(opened["batterySave"].get<std::string>());
    ASSERT_FALSE(save.isEmpty());
    ASSERT_TRUE(waitUntil([&] { return frame(w) > 60; }));
    call(w, "gui.toggle_pause");
    ASSERT_TRUE(waitUntil([&] { return shown(w, "pauseBatterySave"); }));

    button(w, "pauseBatterySave")->click();
    EXPECT_TRUE(waitUntil([&] {
        const QByteArray bytes = readAll(save);
        return bytes.size() > 3 && uchar(bytes[1]) == 0xDE && uchar(bytes[2]) == 0xB0 && uchar(bytes[3]) == 0x61;
    })) << "the save holds the game's signature";
    EXPECT_EQ(runState(w), "Paused");
}

TEST(GuiPauseMenu, WriteBatterySaveNeedsABattery)
{
    MainWindow w(windowOptions());
    w.show();
    call(w, "gui.open_rom", {{"path", kRom.toStdString()}});
    const Result r = w.session().call("gui.write_battery_save", Json::object());
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(r.error->name, "NoBattery");
}

TEST(GuiPauseMenu, QuitAsksFirstAndAClientNeverIsAsked)
{
    MainWindow w(windowOptions());
    w.show();
    openAndPause(w);

    button(w, "pauseQuit")->click();
    QMessageBox* box = nullptr;
    ASSERT_TRUE(waitUntil([&] { return (box = w.findChild<QMessageBox*>(QStringLiteral("pauseQuitConfirmation"))) != nullptr; }));
    box->button(QMessageBox::Cancel)->click();
    waitUntil([] { return false; }, 200);
    EXPECT_TRUE(call(w, "gui.state")["game"].get<bool>()) << "Cancel keeps the game";
    EXPECT_EQ(pauseMenu(w), "full");

    button(w, "pauseQuit")->click();
    ASSERT_TRUE(waitUntil([&] { return (box = w.findChild<QMessageBox*>(QStringLiteral("pauseQuitConfirmation"))) != nullptr; }));
    QAbstractButton* close = nullptr;
    for (QAbstractButton* b : box->buttons()) {
        if (box->buttonRole(b) == QMessageBox::AcceptRole) close = b;
    }
    ASSERT_NE(close, nullptr);
    close->click();
    EXPECT_TRUE(waitUntil([&] { return !call(w, "gui.state")["game"].get<bool>(); }));
    EXPECT_TRUE(waitUntil([&] { return w.isShowingLibrary(); }));

    // A client closes without a dialog.
    openAndPause(w);
    call(w, "gui.close_game");
    EXPECT_FALSE(call(w, "gui.state")["game"].get<bool>());
    EXPECT_EQ(w.findChild<QMessageBox*>(QStringLiteral("pauseQuitConfirmation")), nullptr);
}

TEST(GuiPauseMenu, TheKeyboardMovesBetweenTheButtonsAndEscResumes)
{
    MainWindow w(windowOptions());
    w.show();
    openAndPause(w);
    QAbstractButton* resume = button(w, "pauseResume");
    QAbstractButton* save = button(w, "pauseSave");
    QAbstractButton* quit = button(w, "pauseQuit");
    const auto press = [](QWidget* target, int key) {
        QKeyEvent down(QEvent::KeyPress, key, Qt::NoModifier);
        QKeyEvent up(QEvent::KeyRelease, key, Qt::NoModifier);
        QApplication::sendEvent(target, &down);
        QApplication::sendEvent(target, &up);
    };
    resume->setFocus();
    press(resume, Qt::Key_Down);
    EXPECT_EQ(w.focusWidget(), save);
    press(save, Qt::Key_Down);
    EXPECT_EQ(w.focusWidget(), quit) << "Battery Save is hidden, so skipped";
    press(quit, Qt::Key_Down);
    EXPECT_EQ(w.focusWidget(), resume) << "it wraps round";
    press(resume, Qt::Key_Up);
    EXPECT_EQ(w.focusWidget(), quit);
    press(quit, Qt::Key_Escape);
    EXPECT_TRUE(waitUntil([&] { return runState(w) == "Running"; })) << "Esc resumes";
}
