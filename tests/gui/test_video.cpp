// G050-G051 (spec US7-1..3, FR-040, contract "View"): the video settings.
// - The game area is the chosen whole multiple of the picture, after the
//   pixel aspect and the overscan crop (256x240 less the crop, each pixel
//   1, 8/7 or 4:3-stretch wide).
// - Fullscreen and Always on Top change the window, and toggle back.
// - Nearest keeps each NES pixel one flat color, Smooth blends neighbors,
//   and Scanlines darkens part of every NES line.
// - Frame pacing follows the display when its refresh rate is within 1%
//   of the console's (or a whole multiple), else the console's own rate
//   (R4); "audio" always keeps the console's rate.
// - Every video setting persists, is in gui.get_settings, and is on the
//   Settings > Video page.

#include "gui_support.hpp"
#include "mainwindow.hpp"

#include <cutenes/qt/emulation_view.hpp>

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QImage>
#include <QLabel>
#include <QMenuBar>
#include <QSpinBox>

#include <gtest/gtest.h>

#include <cmath>
#include <set>
#include <vector>

using cutenes::api::Json;
using cutenes::api::Result;
using cutenes::app::MainWindow;
using cutenes::test::waitUntil;
using cutenes::test::windowOptions;

namespace {

Json call(MainWindow& w, const char* name, const Json& args = Json::object())
{
    const Result r = w.session().call(name, args);
    EXPECT_FALSE(r.isError()) << name << ": " << (r.isError() ? r.error->message : "");
    return r.value;
}

QAction* action(MainWindow& w, const char* command, const char* arg = nullptr, const QVariant& value = {})
{
    for (QAction* a : w.menuBar()->findChildren<QAction*>()) {
        if (a->property("command").toString() != QLatin1String(command)) continue;
        if (!arg || a->property("commandArgs").toMap().value(QLatin1String(arg)) == value) return a;
    }
    return nullptr;
}

// The game area for `scale`: (256 - left - right) * aspect by (240 - top -
// bottom), times the scale, rounded up to whole pixels.
QSize expected(int scale, double aspect, int top, int bottom, int left, int right)
{
    return {int(std::ceil((256 - left - right) * aspect * scale - 1e-9)), (240 - top - bottom) * scale};
}

QSize gameArea(MainWindow& w)
{
    return w.centralWidget()->size();
}

} // namespace

TEST(GuiVideo, TheGameAreaIsTheScaleTimesThePicture)
{
    MainWindow w(windowOptions());
    w.show();
    // The defaults: 3x, 8:7 pixels, 8 lines off the top and the bottom.
    ASSERT_TRUE(waitUntil([&] { return gameArea(w) == expected(3, 8.0 / 7.0, 8, 8, 0, 0); }))
        << gameArea(w).width() << "x" << gameArea(w).height();
    EXPECT_TRUE(action(w, "gui.set_scale", "scale", 3)->isChecked());

    EXPECT_EQ(call(w, "gui.set_scale", {{"scale", 2}})["scale"], 2);
    EXPECT_TRUE(waitUntil([&] { return gameArea(w) == expected(2, 8.0 / 7.0, 8, 8, 0, 0); }))
        << gameArea(w).width() << "x" << gameArea(w).height();
    EXPECT_TRUE(waitUntil([&] { return action(w, "gui.set_scale", "scale", 2)->isChecked(); }));
    EXPECT_FALSE(action(w, "gui.set_scale", "scale", 3)->isChecked());

    EXPECT_EQ(call(w, "gui.set_aspect", {{"aspect", "square"}})["aspect"], "square");
    EXPECT_TRUE(waitUntil([&] { return gameArea(w) == expected(2, 1.0, 8, 8, 0, 0); }));
    EXPECT_EQ(call(w, "gui.set_overscan", {{"preset", "none"}})["overscan"],
        Json({{"top", 0}, {"bottom", 0}, {"left", 0}, {"right", 0}}));
    EXPECT_TRUE(waitUntil([&] { return gameArea(w) == QSize(512, 480); }));
    // 4:3 stretches the whole 256x240 picture to 4:3: 640x480 at 2x.
    call(w, "gui.set_aspect", {{"aspect", "4:3"}});
    EXPECT_TRUE(waitUntil([&] { return gameArea(w) == QSize(640, 480); }))
        << gameArea(w).width() << "x" << gameArea(w).height();
    // Each edge on its own.
    EXPECT_EQ(call(w, "gui.set_overscan", {{"top", 16}, {"left", 8}})["overscan"],
        Json({{"top", 16}, {"bottom", 0}, {"left", 8}, {"right", 0}}));
    EXPECT_TRUE(waitUntil([&] { return gameArea(w) == expected(2, (4.0 / 3.0) * 240 / 256, 16, 0, 8, 0); }))
        << gameArea(w).width() << "x" << gameArea(w).height();
    EXPECT_TRUE(waitUntil([&] { return w.view()->displayOptions().overscan.top == 16; }));

    const Result bad = w.session().call("gui.set_overscan", {{"top", 17}});
    ASSERT_TRUE(bad.isError());
    EXPECT_EQ(bad.error->name, "InvalidArguments");
}

TEST(GuiVideo, FullscreenAndAlwaysOnTopToggle)
{
    MainWindow w(windowOptions());
    w.show();
    const QSize windowed = gameArea(w);
    EXPECT_EQ(call(w, "gui.toggle_fullscreen")["fullscreen"], true);
    EXPECT_TRUE(waitUntil([&] { return w.isFullScreen(); }));
    EXPECT_TRUE(action(w, "gui.toggle_fullscreen")->isChecked());
    EXPECT_EQ(call(w, "gui.toggle_fullscreen")["fullscreen"], false);
    EXPECT_TRUE(waitUntil([&] { return !w.isFullScreen() && gameArea(w) == windowed; }))
        << "back to the window's size: " << gameArea(w).width() << "x" << gameArea(w).height();

    EXPECT_EQ(call(w, "gui.toggle_always_on_top")["alwaysOnTop"], true);
    EXPECT_TRUE(waitUntil([&] { return (w.windowFlags() & Qt::WindowStaysOnTopHint) != 0 && w.isVisible(); }));
    EXPECT_TRUE(action(w, "gui.toggle_always_on_top")->isChecked());
    EXPECT_EQ(call(w, "gui.toggle_always_on_top")["alwaysOnTop"], false);
    EXPECT_TRUE(waitUntil([&] { return (w.windowFlags() & Qt::WindowStaysOnTopHint) == 0 && w.isVisible(); }));
}

namespace {

// A picture of the view showing a frame whose left half is black and right
// half white, at 3x with square pixels and no crop: NES pixel (x, y) covers
// screen pixels 3x..3x+2, 3y..3y+2.
QImage halves(MainWindow& w, const char* filter)
{
    call(w, "gui.set_aspect", {{"aspect", "square"}});
    call(w, "gui.set_overscan", {{"preset", "none"}});
    call(w, "gui.set_filter", {{"filter", filter}});
    const auto wanted = QLatin1String(filter) == QLatin1String("smooth") ? cutenes::qt::Filter::Smooth
        : QLatin1String(filter) == QLatin1String("scanlines")             ? cutenes::qt::Filter::Scanlines
                                                                          : cutenes::qt::Filter::Nearest;
    waitUntil([&] {
        const auto o = w.view()->displayOptions();
        return o.overscan.top == 0 && o.aspect == cutenes::qt::Aspect::Square && o.filter == wanted;
    });
    std::vector<std::uint32_t> pixels(256 * 240);
    for (int y = 0; y < 240; ++y) {
        for (int x = 128; x < 256; ++x) pixels[std::size_t(y * 256 + x)] = 0x00ffffff;
    }
    static std::uint64_t index = 1;
    w.view()->onFrame({pixels.data(), ++index}); // grab() paints it, the view being hidden
    w.view()->resize(768, 720);
    QImage image = w.view()->grab().toImage().convertToFormat(QImage::Format_RGB32);
    return image;
}

std::set<int> greys(const QImage& image, int fromX, int toX, int y)
{
    std::set<int> out;
    for (int x = fromX; x < toX; ++x) out.insert(qGray(image.pixel(x, y)));
    return out;
}

} // namespace

TEST(GuiVideo, EachFilterTreatsThePixelsItsWay)
{
    MainWindow w(windowOptions());
    w.show();
    ASSERT_EQ(w.view()->renderer(), cutenes::qt::EmulationView::Renderer::Raster) << "the offscreen tests paint";

    // Nearest: black, then white, nothing between; every line alike.
    QImage image = halves(w, "nearest");
    ASSERT_EQ(image.size(), QSize(768, 720));
    EXPECT_EQ(greys(image, 300, 468, 360), (std::set<int>{0, 255}));
    EXPECT_EQ(greys(image, 600, 601, 0).size(), 1u);
    std::set<int> column;
    for (int y = 300; y < 330; ++y) column.insert(qGray(image.pixel(600, y)));
    EXPECT_EQ(column, (std::set<int>{255}));

    // Smooth: greys between black and white at the edge.
    image = halves(w, "smooth");
    bool between = false;
    for (int g : greys(image, 370, 398, 360)) between = between || (g > 20 && g < 235);
    EXPECT_TRUE(between) << "the edge is blended";
    EXPECT_EQ(call(w, "gui.get_settings", {{"group", "video"}})["values"]["video"]["filter"], "smooth");

    // Scanlines: within each NES line of white, part is darker.
    image = halves(w, "scanlines");
    for (int line = 100; line < 104; ++line) {
        std::set<int> rows;
        for (int y = line * 3; y < line * 3 + 3; ++y) rows.insert(qGray(image.pixel(600, y)));
        EXPECT_GE(rows.size(), 2u) << "NES line " << line;
        EXPECT_EQ(*rows.rbegin(), 255) << "the line's brightest row stays white";
        EXPECT_LT(*rows.begin(), 230) << "and another is darker";
    }
}

TEST(GuiVideo, TheSettingsPersistAndTheVideoPageShowsThem)
{
    QString folder;
    MainWindow::Options options = windowOptions(&folder);
    {
        MainWindow w(options);
        Json defaults = call(w, "gui.get_settings", {{"group", "video"}})["values"]["video"];
        defaults.erase("pacingNow"); // what the pacing does, not a setting
        EXPECT_EQ(defaults, Json({{"scale", 3}, {"fullscreen", false}, {"integerScaling", true}, {"aspect", "8:7"},
                                {"overscan", {{"top", 8}, {"bottom", 8}, {"left", 0}, {"right", 0}}},
                                {"filter", "nearest"}, {"alwaysOnTop", false}, {"pacing", "auto"},
                                {"showFps", false}, {"showOsd", true}}))
            << defaults.dump();
        call(w, "gui.set_setting", {{"key", "video.scale"}, {"value", 4}});
        call(w, "gui.set_setting", {{"key", "video.integerScaling"}, {"value", false}});
        call(w, "gui.set_setting", {{"key", "video.aspect"}, {"value", "4:3"}});
        call(w, "gui.set_setting", {{"key", "video.filter"}, {"value", "scanlines"}});
        call(w, "gui.set_setting",
            {{"key", "video.overscan"}, {"value", {{"top", 0}, {"bottom", 4}, {"left", 8}, {"right", 2}}}});
        for (const auto& [key, value] : std::vector<std::pair<const char*, Json>>{{"video.scale", 7},
                 {"video.aspect", "16:9"}, {"video.filter", "blur"}, {"video.overscan", {{"top", 17}}},
                 {"video.integerScaling", 1}, {"video.pacing", "vsync"}}) {
            const Result bad = w.session().call("gui.set_setting", {{"key", key}, {"value", value}});
            ASSERT_TRUE(bad.isError()) << key;
            EXPECT_EQ(bad.error->data["path"].get<std::string>().rfind("/value", 0), 0u) << key;
        }
    }

    MainWindow again(options);
    again.show();
    const Json video = call(again, "gui.get_settings", {{"group", "video"}})["values"]["video"];
    EXPECT_EQ(video["scale"], 4);
    EXPECT_EQ(video["integerScaling"], false);
    EXPECT_EQ(video["aspect"], "4:3");
    EXPECT_EQ(video["filter"], "scanlines");
    EXPECT_EQ(video["overscan"], Json({{"top", 0}, {"bottom", 4}, {"left", 8}, {"right", 2}}));
    const cutenes::qt::DisplayOptions shown = again.view()->displayOptions();
    EXPECT_EQ(shown.aspect, cutenes::qt::Aspect::FourThree);
    EXPECT_FALSE(shown.integerScaling);
    EXPECT_EQ(shown.filter, cutenes::qt::Filter::Scanlines);
    EXPECT_EQ(shown.overscan.left, 8);
    EXPECT_TRUE(waitUntil([&] { return gameArea(again) == expected(4, (4.0 / 3.0) * 240 / 256, 0, 4, 8, 2); }))
        << gameArea(again).width() << "x" << gameArea(again).height();

    // Settings > Video holds the same, and its controls change them.
    EXPECT_EQ(call(again, "gui.show_settings", {{"page", "video"}})["page"], "video");
    QWidget* dialog = nullptr;
    ASSERT_TRUE(waitUntil([&] {
        return (dialog = again.findChild<QWidget*>(QStringLiteral("settingsDialog"))) && dialog->isVisible();
    }));
    auto* scale = dialog->findChild<QSpinBox*>(QStringLiteral("videoScale"));
    auto* integer = dialog->findChild<QCheckBox*>(QStringLiteral("integerScaling"));
    auto* aspect = dialog->findChild<QComboBox*>(QStringLiteral("aspect"));
    auto* filter = dialog->findChild<QComboBox*>(QStringLiteral("filter"));
    auto* top = dialog->findChild<QSpinBox*>(QStringLiteral("overscanTop"));
    auto* right = dialog->findChild<QSpinBox*>(QStringLiteral("overscanRight"));
    auto* onTop = dialog->findChild<QCheckBox*>(QStringLiteral("alwaysOnTop"));
    auto* fps = dialog->findChild<QCheckBox*>(QStringLiteral("showFps"));
    ASSERT_TRUE(scale && integer && aspect && filter && top && right && onTop && fps);
    EXPECT_EQ(scale->value(), 4);
    EXPECT_FALSE(integer->isChecked());
    EXPECT_EQ(aspect->currentData().toString(), QStringLiteral("4:3"));
    EXPECT_EQ(filter->currentData().toString(), QStringLiteral("scanlines"));
    EXPECT_EQ(top->value(), 0);
    EXPECT_EQ(right->value(), 2);

    integer->click();
    filter->setCurrentIndex(filter->findData(QStringLiteral("smooth")));
    right->setValue(16);
    EXPECT_TRUE(waitUntil([&] {
        const Json v = call(again, "gui.get_settings", {{"group", "video"}})["values"]["video"];
        return v["integerScaling"] == true && v["filter"] == "smooth" && v["overscan"]["right"] == 16;
    })) << call(again, "gui.get_settings", {{"group", "video"}})["values"]["video"].dump();
    EXPECT_TRUE(waitUntil([&] { return again.view()->displayOptions().overscan.right == 16; }));
    EXPECT_TRUE(waitUntil([&] { return action(again, "gui.toggle_integer_scaling")->isChecked(); }));
}

TEST(GuiVideo, CustomOverscanFromTheMenuOpensTheVideoPage)
{
    MainWindow w(windowOptions());
    w.show();
    action(w, "gui.set_overscan", "preset", QStringLiteral("custom"))->trigger();
    QWidget* dialog = nullptr;
    ASSERT_TRUE(waitUntil([&] {
        return (dialog = w.findChild<QWidget*>(QStringLiteral("settingsDialog"))) && dialog->isVisible();
    }));
    EXPECT_TRUE(waitUntil([&] {
        auto* top = dialog->findChild<QSpinBox*>(QStringLiteral("overscanTop"));
        return top && top->isVisible();
    })) << "the dialog is at Video, by the edges";
}

// R4: the pacing in use follows the window's display, and the setting
// chooses it.
TEST(GuiVideo, FramePacingFollowsTheDisplayOrTheSetting)
{
    MainWindow w(windowOptions());
    w.show();
    const auto now = [&] { return call(w, "gui.get_settings", {{"group", "video"}})["values"]["video"]["pacingNow"]; };
    ASSERT_TRUE(waitUntil([&] { return now()["displayHz"].get<double>() > 0; })) << now().dump();
    const double hz = now()["displayHz"].get<double>();
    const bool near60 = std::abs(hz - 60.0988) <= 0.600988 || std::abs(hz / 2 - 60.0988) <= 0.600988;
    EXPECT_EQ(now()["mode"], near60 ? "display" : "audio") << hz << " Hz";
    if (near60) {
        EXPECT_NEAR(now()["framesPerSecond"].get<double>() * now()["divisor"].get<int>(), hz, 1e-6);
        EXPECT_NEAR(w.frameRate(), now()["framesPerSecond"].get<double>(), 1e-4) << "the clock runs at it";
    }

    call(w, "gui.set_setting", {{"key", "video.pacing"}, {"value", "audio"}});
    EXPECT_TRUE(waitUntil([&] { return now()["mode"] == "audio"; }));
    EXPECT_DOUBLE_EQ(now()["framesPerSecond"].get<double>(), 60.0988);
    EXPECT_NEAR(w.frameRate(), 60.0988, 1e-4); // the clock's period is whole nanoseconds

    // Settings > Video shows it, in words too.
    call(w, "gui.show_settings", {{"page", "video"}});
    QWidget* dialog = nullptr;
    ASSERT_TRUE(waitUntil([&] {
        return (dialog = w.findChild<QWidget*>(QStringLiteral("settingsDialog"))) && dialog->isVisible();
    }));
    auto* pacing = dialog->findChild<QComboBox*>(QStringLiteral("pacing"));
    auto* words = dialog->findChild<QLabel*>(QStringLiteral("pacingNow"));
    ASSERT_TRUE(pacing && words);
    EXPECT_EQ(pacing->currentData().toString(), QStringLiteral("audio"));
    EXPECT_TRUE(words->text().contains(QStringLiteral("console's own rate"))) << words->text().toStdString();
    pacing->setCurrentIndex(pacing->findData(QStringLiteral("auto")));
    EXPECT_TRUE(waitUntil([&] { return now()["mode"] == (near60 ? "display" : "audio"); }));
    EXPECT_EQ(call(w, "gui.get_settings", {{"group", "video"}})["values"]["video"]["pacing"], "auto");
}
