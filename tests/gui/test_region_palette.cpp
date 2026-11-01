// G060-G061 (spec FR-041, menus-and-commands.md, core-additions.md): Emulation
// > Region and View > Palette.
// - Region forces NTSC or PAL from the next power-on, and the frame clock
//   follows the region in effect.
// - The palette is a built-in one or a .pal file of 64 or 512 RGB triplets
//   (nesdev "PPU palettes"); the picture takes its colours.
// - Both persist, and both are in gui.get_settings.

#include "gui_support.hpp"
#include "mainwindow.hpp"
#include "media/palettes.hpp"

#include <cutenes/qt/emulation_view.hpp>

#include <QAction>
#include <QFile>
#include <QMenuBar>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

using cutenes::api::Json;
using cutenes::api::Result;
using cutenes::app::MainWindow;
using cutenes::test::waitUntil;
using cutenes::test::windowOptions;

namespace {

constexpr double kNtsc = 60.0988;
constexpr double kPal = 50.0070;
const QString kRom = QStringLiteral(CUTENES_TEST_ROMS "/instr_test-v5/rom_singles/01-basics.nes");

Json call(MainWindow& w, const char* name, const Json& args = Json::object())
{
    const Result r = w.session().call(name, args);
    EXPECT_FALSE(r.isError()) << name << ": " << (r.isError() ? r.error->message : "");
    return r.value;
}

QAction* action(MainWindow& w, const char* command, const char* arg, const QVariant& value)
{
    for (QAction* a : w.menuBar()->findChildren<QAction*>()) {
        if (a->property("command").toString() == QLatin1String(command)
            && a->property("commandArgs").toMap().value(QLatin1String(arg)) == value) {
            return a;
        }
    }
    return nullptr;
}

} // namespace

// nesdev "PPU palettes": the RGB PPU's entry $00 is 333 and $21 is 357,
// each digit a 3-bit level, C = 255 * DAC / 7.
TEST(GuiPalette, TheRgbPaletteIsTheRgbPpusTable)
{
    const auto rgb = cutenes::app::builtinPalette(QStringLiteral("rgb"));
    ASSERT_TRUE(rgb.has_value());
    ASSERT_EQ(rgb->size(), 64u);
    EXPECT_EQ((*rgb)[0x00], 0x6D6D6Du); // 3 -> 109
    EXPECT_EQ((*rgb)[0x21], 0x6DB6FFu); // 3, 5, 7 -> 109, 182, 255
    EXPECT_EQ((*rgb)[0x0F], 0x000000u);
    EXPECT_TRUE(cutenes::app::builtinPalette(QStringLiteral("default"))->empty());
    EXPECT_FALSE(cutenes::app::builtinPalette(QStringLiteral("sepia")).has_value());
}

TEST(GuiRegion, PalFromTheMenuRetimesTheGameAtThePowerCycle)
{
    QString folder;
    const MainWindow::Options options = windowOptions(&folder);
    {
        MainWindow w(options);
        w.show();
        call(w, "gui.set_setting", {{"key", "video.pacing"}, {"value", "audio"}}); // the console's own rate
        EXPECT_EQ(call(w, "gui.get_settings")["values"]["emulation"]["region"], "auto");
        call(w, "gui.set_region", {{"region", "pal"}});
        call(w, "gui.open_rom", {{"path", kRom.toStdString()}});
        EXPECT_EQ(call(w, "core.region")["region"], "PAL") << "the header says NTSC";
        EXPECT_TRUE(waitUntil([&] { return std::abs(w.frameRate() - kPal) < 0.01; })) << w.frameRate();
        EXPECT_TRUE(waitUntil([&] { return action(w, "gui.set_region", "region", "pal")->isChecked(); }));

        const Json changed = call(w, "gui.set_region", {{"region", "auto"}});
        EXPECT_EQ(changed["region"], "PAL") << "a running game keeps its timing";
        EXPECT_EQ(changed["pending"], true);
        call(w, "gui.power_cycle");
        EXPECT_EQ(call(w, "core.region")["region"], "NTSC");
        EXPECT_TRUE(waitUntil([&] { return std::abs(w.frameRate() - kNtsc) < 0.01; })) << w.frameRate();
        call(w, "gui.set_region", {{"region", "pal"}});
    }
    MainWindow again(options);
    EXPECT_EQ(call(again, "gui.get_settings")["values"]["emulation"]["region"], "pal");
    EXPECT_TRUE(again.session().call("gui.set_region", {{"region", "secam"}}).isError());
}

TEST(GuiPalette, APalFileColoursThePictureAndPersists)
{
    QString folder;
    const MainWindow::Options options = windowOptions(&folder);
    const QString pal = folder + QStringLiteral("/teal.pal");
    {
        QFile file(pal);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        for (int i = 0; i < 64; ++i) file.write("\x10\x80\x80", 3);
    }
    const QString bad = folder + QStringLiteral("/short.pal");
    {
        QFile file(bad);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        file.write(QByteArray(100, '\0'));
    }
    {
        MainWindow w(options);
        w.show();
        EXPECT_EQ(call(w, "gui.get_settings")["values"]["video"]["palette"], "default");
        call(w, "gui.set_palette", {{"path", pal.toStdString()}});
        call(w, "gui.open_rom", {{"path", kRom.toStdString()}});
        EXPECT_TRUE(waitUntil([&] {
            const auto& pixels = w.view()->shownPixels();
            return !pixels.empty() && std::all_of(pixels.begin(), pixels.end(), [](std::uint32_t p) {
                return (p & 0xFFFFFF) == 0x108080;
            });
        })) << "every colour is the file's";

        const Result refused = w.session().call("gui.set_palette", {{"path", bad.toStdString()}});
        ASSERT_TRUE(refused.isError());
        EXPECT_NE(refused.error->message.find("192 or 1536"), std::string::npos) << refused.error->message;
        EXPECT_EQ(call(w, "gui.get_settings")["values"]["video"]["palette"], pal.toStdString()) << "unchanged";
        EXPECT_TRUE(w.session().call("gui.set_palette", {{"name", "sepia"}}).isError());

        call(w, "gui.set_palette", {{"name", "rgb"}});
        EXPECT_TRUE(waitUntil([&] { return action(w, "gui.set_palette", "name", "rgb")->isChecked(); }));
    }
    MainWindow again(options);
    EXPECT_EQ(call(again, "gui.get_settings")["values"]["video"]["palette"], "rgb");
}
