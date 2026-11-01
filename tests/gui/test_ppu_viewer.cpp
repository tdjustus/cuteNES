// G080/G083 (spec US9-4, FR-062, quickstart A13): the PPU viewers.
// Expectations come from the hardware's formats (nesdev "PPU pattern
// tables", "PPU nametables", "PPU attribute tables", "PPU OAM", "PPU
// palettes") applied to bytes the test reads or writes itself, never from
// the viewer's own output:
// - both pattern tables equal the test ROM's CHR ROM, decoded here, in the
//   chosen palette's colours (colour 0 is the backdrop). The ROM is made
//   here: its CHR mixes all four values with different planes, which a
//   font (nestest's, say) does not, so a plane mix-up shows;
// - a nametable tile and its attribute palette appear where the format
//   puts them, and the scroll the game set is shown;
// - a sprite is listed with its OAM bytes and drawn flipped as its
//   attributes say;
// - the palette tab lists palette RAM;
// - the views follow the PPU while the game runs.

#include "gui_support.hpp"
#include "mainwindow.hpp"

#include <cutenes/media.hpp>

#include <QComboBox>
#include <QDialog>
#include <QFile>
#include <QLabel>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>

#include <cstring>

#include <gtest/gtest.h>

using cutenes::api::Json;
using cutenes::api::Result;
using cutenes::app::MainWindow;
using cutenes::test::waitUntil;
using cutenes::test::windowOptions;

namespace {

// The CHR ROM: tile t, row y has planes that differ and use all four values.
QByteArray testChr()
{
    QByteArray chr(8192, '\0');
    for (int tile = 0; tile < 512; ++tile) {
        for (int y = 0; y < 8; ++y) {
            chr[tile * 16 + y] = static_cast<char>((tile * 7 + y * 13 + 0x35) & 0xFF);     // low plane
            chr[tile * 16 + 8 + y] = static_cast<char>((tile * 11 + y * 29 + 0xA6) & 0xFF); // high plane
        }
    }
    return chr;
}

// NROM-128 with that CHR, whose program only loops (JMP $C000): it never
// touches the PPU, so what the test writes stays.
QByteArray testRom()
{
    QByteArray rom(16 + 16384, '\0');
    const char header[] = {'N', 'E', 'S', 0x1A, 1, 1};
    std::memcpy(rom.data(), header, sizeof header);
    rom[16] = static_cast<char>(0x4C);
    rom[17] = 0x00;
    rom[18] = static_cast<char>(0xC0);
    rom[16 + 0x3FFC] = 0x00;
    rom[16 + 0x3FFD] = static_cast<char>(0xC0);
    return rom + testChr();
}

Json call(MainWindow& w, const char* name, const Json& args = Json::object())
{
    const Result r = w.session().call(name, args);
    EXPECT_FALSE(r.isError()) << name << ": " << (r.isError() ? r.error->message : "");
    return r.value;
}

QByteArray bytes(const Json& v)
{
    return QByteArray::fromBase64(QByteArray::fromStdString(v["bytes"].get<std::string>()));
}

std::string b64(const QByteArray& raw)
{
    return raw.toBase64().toStdString();
}

// The 2-bit value of pixel (x, y) of tile `tile`: bit 7 of each plane byte
// is the leftmost pixel, the low plane first (nesdev "PPU pattern tables").
int chrPixel(const QByteArray& chr, int tile, int x, int y)
{
    const auto plane = [&](int offset) { return static_cast<unsigned char>(chr[tile * 16 + offset + y]) >> (7 - x) & 1; };
    return plane(0) | plane(8) << 1;
}

std::uint32_t colour(const QByteArray& paletteRam, int entry)
{
    return cutenes::defaultPalette()[static_cast<unsigned char>(paletteRam[entry]) & 0x3F] & 0xFFFFFF;
}

// Palette `p`'s colour `value`: 0 is always the backdrop.
std::uint32_t paletteColour(const QByteArray& paletteRam, int p, int value)
{
    return colour(paletteRam, value == 0 ? 0 : p * 4 + value);
}

std::uint32_t rgb(const QImage& image, int x, int y)
{
    return image.pixel(x, y) & 0xFFFFFF;
}

QDialog* openViewer(MainWindow& w, const char* view)
{
    call(w, "gui.show_ppu_viewer", {{"view", view}});
    QDialog* viewer = nullptr;
    EXPECT_TRUE(waitUntil([&] {
        viewer = w.findChild<QDialog*>(QStringLiteral("ppuViewer"));
        return viewer && viewer->isVisible();
    }));
    return viewer;
}

QImage shown(QDialog* viewer, const char* label)
{
    auto* l = viewer->findChild<QLabel*>(QString::fromLatin1(label));
    return l ? l->pixmap().toImage() : QImage();
}

void openPaused(MainWindow& w)
{
    static QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("chr.nes"));
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write(testRom());
    file.close();
    w.show();
    call(w, "gui.set_setting", {{"key", "emulation.pauseWhenInactive"}, {"value", false}});
    call(w, "gui.open_rom", {{"path", path.toStdString()}});
    ASSERT_TRUE(waitUntil([&] { return call(w, "core.run_state")["frame"].get<int>() >= 3; }));
    call(w, "gui.toggle_pause");
}

// A palette RAM the test can tell apart: every entry different.
QByteArray distinctPalette()
{
    QByteArray pal;
    for (int i = 0; i < 32; ++i) pal.append(static_cast<char>((i * 5 + 1) & 0x3F));
    pal[0] = 0x0F;
    return pal;
}

} // namespace

TEST(GuiPpuViewer, ThePatternTablesEqualTheChrData)
{
    MainWindow w(windowOptions());
    openPaused(w);
    call(w, "core.write_palette", {{"bytes", b64(distinctPalette())}});
    // As the PPU holds it: $3F10/$14/$18/$1C mirror $3F00/$04/$08/$0C.
    const QByteArray pal = bytes(call(w, "core.read_palette"));
    QDialog* viewer = openViewer(w, "patterns");
    ASSERT_TRUE(viewer);
    auto* tabs = viewer->findChild<QTabWidget*>(QStringLiteral("ppuTabs"));
    ASSERT_TRUE(tabs);
    EXPECT_EQ(tabs->currentWidget()->objectName(), QStringLiteral("patterns"));

    const QByteArray chr = testChr();
    for (int p : {0, 2, 6}) {
        auto* choice = viewer->findChild<QComboBox*>(QStringLiteral("patternPalette"));
        ASSERT_TRUE(choice);
        choice->setCurrentIndex(p);
        for (int table = 0; table < 2; ++table) {
            const char* label = table == 0 ? "patternTable0" : "patternTable1";
            // Every pixel of all 256 tiles, once the viewer has drawn this palette.
            int mismatches = -1;
            EXPECT_TRUE(waitUntil([&] {
                const QImage image = shown(viewer, label);
                if (image.size() != QSize(128, 128)) return false;
                mismatches = 0;
                for (int tile = 0; tile < 256; ++tile) {
                    for (int y = 0; y < 8; ++y) {
                        for (int x = 0; x < 8; ++x) {
                            const std::uint32_t want = paletteColour(pal, p, chrPixel(chr, table * 256 + tile, x, y));
                            if (rgb(image, tile % 16 * 8 + x, tile / 16 * 8 + y) != want) ++mismatches;
                        }
                    }
                }
                return mismatches == 0;
            })) << label << " in palette " << p << ": " << mismatches << " pixels differ";
        }
    }
}

TEST(GuiPpuViewer, NametablesShowTheTilesTheirAttributesAndTheScroll)
{
    MainWindow w(windowOptions());
    openPaused(w);
    call(w, "core.write_palette", {{"bytes", b64(distinctPalette())}});
    // As the PPU holds it: $3F10/$14/$18/$1C mirror $3F00/$04/$08/$0C.
    const QByteArray pal = bytes(call(w, "core.read_palette"));
    // Background tiles from $0000 (PPUCTRL bit 4 clear), scroll X 125, Y 70
    // in nametable 0: coarse X 15 + fine 5, coarse Y 8 + fine 6.
    call(w, "core.write_cpu_register", {{"addr", 0x2000}, {"value", 0x00}});
    call(w, "core.write_cpu_register", {{"addr", 0x2005}, {"value", 125}});
    call(w, "core.write_cpu_register", {{"addr", 0x2005}, {"value", 70}});
    // Nametable 1 ($2400), row 6, column 9: tile $41. Its attribute byte is
    // $2400 + $3C0 + (6 / 4) * 8 + 9 / 4 = $27CA; the tile is in the
    // bottom-left 2 x 2 of that byte (row 6 % 4 = 2, column 9 % 4 = 1), bits
    // 4-5: palette 2.
    call(w, "core.write_vram", {{"addr", 0x2400 + 6 * 32 + 9}, {"bytes", b64(QByteArray(1, 0x41))}});
    call(w, "core.write_vram", {{"addr", 0x27CA}, {"bytes", b64(QByteArray(1, 0x20))}});

    QDialog* viewer = openViewer(w, "nametables");
    ASSERT_TRUE(viewer);
    auto* scroll = viewer->findChild<QLabel*>(QStringLiteral("scrollPosition"));
    ASSERT_TRUE(scroll);
    EXPECT_TRUE(waitUntil([&] { return scroll->text().contains(QStringLiteral("125")) && scroll->text().contains(QStringLiteral("70")); }))
        << scroll->text().toStdString();

    const QByteArray chr = testChr();
    ASSERT_TRUE(waitUntil([&] { return shown(viewer, "nametableView").size() == QSize(512, 480); }));
    const int left = 256 + 9 * 8; // nametable 1 is the top right
    const int top = 6 * 8;
    int mismatches = 0;
    EXPECT_TRUE(waitUntil([&] {
        const QImage image = shown(viewer, "nametableView");
        mismatches = 0;
        for (int y = 0; y < 8; ++y) {
            for (int x = 0; x < 8; ++x) {
                if (rgb(image, left + x, top + y) != paletteColour(pal, 2, chrPixel(chr, 0x41, x, y))) ++mismatches;
            }
        }
        return mismatches == 0;
    })) << mismatches << " pixels of tile $41 differ";
}

TEST(GuiPpuViewer, SpritesAreListedAndDrawnAsTheirAttributesSay)
{
    MainWindow w(windowOptions());
    openPaused(w);
    call(w, "core.write_palette", {{"bytes", b64(distinctPalette())}});
    // As the PPU holds it: $3F10/$14/$18/$1C mirror $3F00/$04/$08/$0C.
    const QByteArray pal = bytes(call(w, "core.read_palette"));
    call(w, "core.write_cpu_register", {{"addr", 0x2000}, {"value", 0x08}}); // 8 x 8 sprites from $1000
    // Sprite 3: Y 50, tile $21, palette 1 (4 + 1) flipped horizontally, X 100.
    QByteArray oam = bytes(call(w, "core.read_oam"));
    oam[12] = 50;
    oam[13] = 0x21;
    oam[14] = 0x41;
    oam[15] = 100;
    call(w, "core.write_oam", {{"bytes", b64(oam)}});

    QDialog* viewer = openViewer(w, "sprites");
    ASSERT_TRUE(viewer);
    auto* table = viewer->findChild<QTableWidget*>(QStringLiteral("spriteTable"));
    ASSERT_TRUE(table);
    EXPECT_EQ(table->rowCount(), 64);
    ASSERT_TRUE(waitUntil([&] { return table->item(3, 1) && table->item(3, 1)->text() == QStringLiteral("100"); }));
    EXPECT_EQ(table->item(3, 2)->text(), QStringLiteral("50"));
    EXPECT_EQ(table->item(3, 3)->text(), QStringLiteral("21"));
    EXPECT_EQ(table->item(3, 4)->text(), QStringLiteral("41"));

    const QByteArray chr = testChr();
    const QImage image = shown(viewer, "spriteView");
    ASSERT_EQ(image.size(), QSize(64, 128));
    int mismatches = 0;
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            // Flipped: the cell's pixel x is the tile's 7 - x.
            const std::uint32_t want = paletteColour(pal, 5, chrPixel(chr, 256 + 0x21, 7 - x, y));
            if (rgb(image, 3 * 8 + x, y) != want) ++mismatches;
        }
    }
    EXPECT_EQ(mismatches, 0);
}

TEST(GuiPpuViewer, ThePaletteTabListsPaletteRamAndViewsFollowTheRunningGame)
{
    MainWindow w(windowOptions());
    openPaused(w);
    call(w, "core.write_palette", {{"bytes", b64(distinctPalette())}});
    // As the PPU holds it: $3F10/$14/$18/$1C mirror $3F00/$04/$08/$0C.
    const QByteArray pal = bytes(call(w, "core.read_palette"));
    QDialog* viewer = openViewer(w, "palette");
    ASSERT_TRUE(viewer);
    auto* table = viewer->findChild<QTableWidget*>(QStringLiteral("paletteTable"));
    ASSERT_TRUE(table);
    ASSERT_TRUE(waitUntil([&] { return table->item(1, 3) && table->item(1, 3)->text() == QStringLiteral("%1").arg(static_cast<unsigned char>(pal[19]), 2, 16, QLatin1Char('0')).toUpper(); }));
    EXPECT_EQ(table->item(0, 1)->background().color().rgb() & 0xFFFFFF, colour(pal, 1));

    // Running: the views follow the PPU. The program leaves OAM alone, so a
    // sprite changed while it runs stays changed.
    call(w, "gui.show_ppu_viewer", {{"view", "sprites"}});
    auto* sprites = viewer->findChild<QTableWidget*>(QStringLiteral("spriteTable"));
    ASSERT_TRUE(sprites);
    call(w, "gui.toggle_pause");
    ASSERT_EQ(call(w, "core.run_state")["runState"], "Running");
    QByteArray oam = bytes(call(w, "core.read_oam"));
    oam[63 * 4 + 3] = static_cast<char>(222); // sprite 63's X
    call(w, "core.write_oam", {{"bytes", b64(oam)}});
    EXPECT_TRUE(waitUntil([&] { return sprites->item(63, 1) && sprites->item(63, 1)->text() == QStringLiteral("222"); }, 2000));
}
