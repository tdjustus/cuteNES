// G033: Take Screenshot, Show FPS, Show On-Screen Messages, and battery save
// import and export (spec FR-013, FR-043, the menus contract, library-layout
// "screenshots/" and ".sav"). A screenshot is the core's own frame, pixel for
// pixel; an exported save is the cartridge's battery RAM, byte for byte.

#include "gui_support.hpp"
#include "mainwindow.hpp"

#include <cutenes/qt/emulation_view.hpp>

#include <QAction>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QLabel>
#include <QMenuBar>
#include <QRegularExpression>

#include <gtest/gtest.h>

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

std::uint64_t frame(MainWindow& w)
{
    return call(w, "core.counters")["frame"].get<std::uint64_t>();
}

QByteArray blob(MainWindow& w, const std::string& blobId)
{
    QByteArray bytes;
    for (bool eof = false; !eof;) {
        const Json part = call(w, "session.blob_read", {{"blobId", blobId}, {"offset", bytes.size()}, {"length", 65536}});
        bytes += QByteArray::fromBase64(QByteArray::fromStdString(part["bytes"].get<std::string>()));
        eof = part["eof"].get<bool>();
    }
    return bytes;
}

QByteArray sram(MainWindow& w)
{
    const auto size = call(w, "core.read_sram", {{"offset", 0}, {"length", 1}})["size"].get<int>();
    const Json all = call(w, "core.read_sram", {{"offset", 0}, {"length", size}});
    return QByteArray::fromBase64(QByteArray::fromStdString(all["bytes"].get<std::string>()));
}

QByteArray readAll(const QString& path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

bool writeAll(const QString& path, const QByteArray& bytes)
{
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size();
}

// 01-basics with the header's battery bit set: its $6000 status area is
// then battery RAM (as in test_battery.cpp).
QString batteryRom(const QString& folder)
{
    QByteArray rom = readAll(kRom);
    rom[6] = char(rom[6] | 0x02);
    const QString path = folder + QStringLiteral("/battery-basics.nes");
    EXPECT_TRUE(writeAll(path, rom));
    return path;
}

QAction* action(MainWindow& w, const char* command)
{
    for (QAction* a : w.menuBar()->findChildren<QAction*>()) {
        if (a->property("command").toString() == QLatin1String(command)) return a;
    }
    return nullptr;
}

} // namespace

TEST(GuiTools, AScreenshotIsTheCoresFrameAtNativeSize)
{
    QString folder;
    MainWindow w(windowOptions(&folder));
    w.show();
    call(w, "gui.open_rom", {{"path", kRom.toStdString()}});
    ASSERT_TRUE(waitUntil([&] { return frame(w) > 60; }));
    call(w, "core.pause");
    const Json shot = call(w, "gui.take_screenshot");
    const QString path = QString::fromStdString(shot["path"].get<std::string>());
    EXPECT_TRUE(path.startsWith(folder + QStringLiteral("/library/screenshots/"))) << path.toStdString();
    EXPECT_TRUE(path.endsWith(QStringLiteral(".png")));
    EXPECT_TRUE(QRegularExpression(QStringLiteral("/\\d{8}-\\d{6}(-\\d+)?\\.png$")).match(path).hasMatch())
        << "screenshots/<game>/<YYYYMMDD-HHMMSS>.png: " << path.toStdString();

    QImage image(path);
    ASSERT_FALSE(image.isNull());
    EXPECT_EQ(image.size(), QSize(256, 240)) << "FR-043: native, no scaling";
    const Json screen = call(w, "core.screen", {{"format", "raw"}});
    const QByteArray raw = blob(w, screen["blobId"].get<std::string>());
    ASSERT_EQ(raw.size(), 256 * 240 * 4);
    const QImage core(reinterpret_cast<const uchar*>(raw.constData()), 256, 240, QImage::Format_RGB32);
    EXPECT_EQ(image.convertToFormat(QImage::Format_RGB32), core) << "pixel for pixel";

    // A second one in the same second gets its own file.
    const QString second = QString::fromStdString(call(w, "gui.take_screenshot")["path"].get<std::string>());
    EXPECT_NE(second, path);
    EXPECT_TRUE(QFile::exists(path));
    EXPECT_TRUE(QFile::exists(second));
}

TEST(GuiTools, AScreenshotNeedsAGame)
{
    MainWindow w(windowOptions());
    const Result r = w.session().call("gui.take_screenshot", Json::object());
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(r.error->name, "NoCartridge");
}

TEST(GuiTools, ShowFpsShowsTheRateTheGameRunsAt)
{
    auto options = windowOptions();
    {
        MainWindow w(options);
        w.show();
        call(w, "gui.open_rom", {{"path", kRom.toStdString()}});
        auto* fps = w.findChild<QLabel*>(QStringLiteral("fpsOverlay"));
        ASSERT_NE(fps, nullptr);
        EXPECT_FALSE(fps->isVisible()) << "data-model: showFps is off by default";
        EXPECT_EQ(call(w, "gui.toggle_fps")["showFps"], true);
        EXPECT_TRUE(waitUntil([&] { return action(w, "gui.toggle_fps")->isChecked(); }));
        const QRegularExpression rate(QStringLiteral("^(\\d+) FPS$"));
        // NTSC is 60.0988 fps; the number is the frames shown in the last
        // second, so it settles near 60.
        EXPECT_TRUE(waitUntil([&] {
            const auto m = rate.match(fps->text());
            return fps->isVisible() && m.hasMatch() && m.captured(1).toInt() >= 50 && m.captured(1).toInt() <= 70;
        }, 5000)) << fps->text().toStdString();
        EXPECT_EQ(fps->parentWidget(), w.view());
        EXPECT_LT(fps->geometry().left(), w.view()->width() / 10) << "top left";
        EXPECT_LT(fps->geometry().top(), w.view()->height() / 4);
    }
    {
        MainWindow w(options); // the choice is kept
        w.show();
        call(w, "gui.open_rom", {{"path", kRom.toStdString()}});
        auto* fps = w.findChild<QLabel*>(QStringLiteral("fpsOverlay"));
        EXPECT_TRUE(action(w, "gui.toggle_fps")->isChecked());
        EXPECT_TRUE(waitUntil([&] { return fps->isVisible(); }));
        EXPECT_EQ(call(w, "gui.toggle_fps")["showFps"], false);
        EXPECT_TRUE(waitUntil([&] { return !fps->isVisible(); }));
    }
}

TEST(GuiTools, OnScreenMessagesCanBeTurnedOff)
{
    MainWindow w(windowOptions());
    w.show();
    call(w, "gui.open_rom", {{"path", kRom.toStdString()}});
    ASSERT_TRUE(waitUntil([&] { return frame(w) > 10; }));
    auto* osd = w.findChild<QLabel*>(QStringLiteral("osdMessage"));
    ASSERT_NE(osd, nullptr);
    EXPECT_TRUE(action(w, "gui.toggle_osd")->isChecked()) << "data-model: showOsd is on by default";
    EXPECT_EQ(call(w, "gui.toggle_osd")["showOsd"], false);
    EXPECT_TRUE(waitUntil([&] { return !action(w, "gui.toggle_osd")->isChecked(); }));
    call(w, "gui.save_state", {{"slot", 1}});
    QCoreApplication::processEvents();
    EXPECT_FALSE(osd->isVisible()) << "no message while they are off";
    EXPECT_EQ(call(w, "gui.toggle_osd")["showOsd"], true);
    call(w, "gui.save_state", {{"slot", 2}});
    EXPECT_TRUE(waitUntil([&] { return osd->isVisible(); }));
    // Messages look like the speed indicator; only the place differs.
    auto* speed = w.findChild<QLabel*>(QStringLiteral("speedOverlay"));
    ASSERT_NE(speed, nullptr);
    EXPECT_EQ(osd->styleSheet(), speed->styleSheet());
}

TEST(GuiTools, ExportWritesTheBatteryRamAndImportReplacesIt)
{
    QString folder;
    MainWindow w(windowOptions(&folder));
    w.show();
    const QString rom = batteryRom(folder);
    const QString saved = QString::fromStdString(
        call(w, "gui.open_rom", {{"path", rom.toStdString()}})["batterySave"].get<std::string>());
    ASSERT_FALSE(saved.isEmpty());
    // The test ROM writes its status signature (DE B0 61) at $6001.
    ASSERT_TRUE(waitUntil([&] { return sram(w).mid(1, 3) == QByteArray("\xDE\xB0\x61", 3); }, 10000));
    call(w, "core.pause");

    const QString exported = folder + QStringLiteral("/exported.sav");
    EXPECT_EQ(call(w, "gui.export_battery_save", {{"path", exported.toStdString()}})["path"], exported.toStdString());
    const QByteArray ram = sram(w);
    EXPECT_EQ(readAll(exported), ram) << "FR-013: raw battery RAM";

    QByteArray other(ram.size(), '\0');
    for (int i = 0; i < other.size(); ++i) other[i] = char(i * 7 + 3);
    const QString imported = folder + QStringLiteral("/other.sav");
    ASSERT_TRUE(writeAll(imported, other));
    call(w, "gui.import_battery_save", {{"path", imported.toStdString()}});
    EXPECT_EQ(call(w, "core.run_state")["runState"], "Paused") << "a paused game stays paused";
    EXPECT_EQ(sram(w), other);
    // The library's own save follows at once, even while paused.
    EXPECT_TRUE(waitUntil([&] { return readAll(saved) == other; }));

    // A short save is padded with zeros, a long one trimmed (library-layout ".sav").
    ASSERT_TRUE(writeAll(imported, QByteArray("\x01\x02\x03", 3)));
    call(w, "gui.import_battery_save", {{"path", imported.toStdString()}});
    QByteArray padded(ram.size(), '\0');
    padded[0] = 1;
    padded[1] = 2;
    padded[2] = 3;
    EXPECT_EQ(sram(w), padded);
}

TEST(GuiTools, BatteryImportAndExportRefuseWhatTheyCannotDo)
{
    QString folder;
    MainWindow w(windowOptions(&folder));
    w.show();
    call(w, "gui.open_rom", {{"path", kRom.toStdString()}}); // no battery
    auto fails = [&](const char* name, const QString& path) {
        const Result r = w.session().call(name, {{"path", path.toStdString()}});
        return r.isError() ? r.error->name : std::string("ok");
    };
    EXPECT_EQ(fails("gui.export_battery_save", folder + QStringLiteral("/x.sav")), "NoBattery");
    EXPECT_EQ(fails("gui.import_battery_save", folder + QStringLiteral("/x.sav")), "NoBattery");
    EXPECT_FALSE(QFile::exists(folder + QStringLiteral("/x.sav")));

    call(w, "gui.open_rom", {{"path", batteryRom(folder).toStdString()}});
    EXPECT_EQ(fails("gui.import_battery_save", folder + QStringLiteral("/missing.sav")), "NotFound");
    EXPECT_EQ(fails("gui.export_battery_save", QStringLiteral("relative.sav")), "InvalidArguments");
}
