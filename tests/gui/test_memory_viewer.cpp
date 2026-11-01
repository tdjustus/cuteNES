// G080/G082 (spec US9-3, FR-061): the memory viewer and editor.
// - Viewing never changes the machine: with the game paused, the viewer
//   reading every space (CPU memory over the PPU registers included, where a
//   real read of $2002 clears VBlank and one of $2007 moves the address)
//   leaves the save state byte for byte as it was.
// - The table shows the bytes there, 16 to a row, in hex.
// - An edit in each space (CPU memory, PPU memory, OAM, palette RAM) reaches
//   that memory.

#include "gui_support.hpp"
#include "mainwindow.hpp"

#include <QComboBox>
#include <QDialog>
#include <QLineEdit>
#include <QTableWidget>

#include <gtest/gtest.h>

using cutenes::api::Json;
using cutenes::api::Result;
using cutenes::app::MainWindow;
using cutenes::test::waitUntil;
using cutenes::test::windowOptions;

namespace {

const QString kNestest = QStringLiteral(CUTENES_TEST_ROMS "/other/nestest.nes");

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

// The whole save state, through the API.
QByteArray state(MainWindow& w)
{
    const Json blob = call(w, "core.capture_state");
    const std::string id = blob["blobId"].get<std::string>();
    QByteArray all;
    for (bool eof = false; !eof;) {
        const Json part = call(w, "session.blob_read", {{"blobId", id}, {"offset", all.size()}, {"length", 65536}});
        all += bytes(part);
        eof = part["eof"].get<bool>();
    }
    call(w, "session.blob_delete", {{"blobId", id}});
    return all;
}

struct Viewer {
    QDialog* window = nullptr;
    QComboBox* space = nullptr;
    QLineEdit* address = nullptr;
    QTableWidget* table = nullptr;
};

Viewer openViewer(MainWindow& w)
{
    call(w, "gui.show_memory_viewer");
    Viewer v;
    EXPECT_TRUE(waitUntil([&] {
        v.window = w.findChild<QDialog*>(QStringLiteral("memoryViewer"));
        return v.window && v.window->isVisible();
    }));
    if (!v.window) return v;
    v.space = v.window->findChild<QComboBox*>(QStringLiteral("memorySpace"));
    v.address = v.window->findChild<QLineEdit*>(QStringLiteral("memoryAddress"));
    v.table = v.window->findChild<QTableWidget*>(QStringLiteral("memoryTable"));
    EXPECT_TRUE(v.space && v.address && v.table);
    return v;
}

void goTo(Viewer& v, const QString& space, unsigned address)
{
    v.space->setCurrentIndex(v.space->findData(space));
    v.address->setText(QStringLiteral("%1").arg(address, 4, 16, QLatin1Char('0')));
    emit v.address->editingFinished();
}

// The cell for the byte at `offset` from the row shown first.
QTableWidgetItem* cell(Viewer& v, int offset)
{
    return v.table->item(offset / 16, offset % 16);
}

void openPaused(MainWindow& w)
{
    w.show();
    call(w, "gui.set_setting", {{"key", "emulation.pauseWhenInactive"}, {"value", false}});
    call(w, "gui.open_rom", {{"path", kNestest.toStdString()}});
    ASSERT_TRUE(waitUntil([&] { return call(w, "core.run_state")["frame"].get<int>() >= 3; }));
    call(w, "gui.toggle_pause");
}

} // namespace

TEST(GuiMemoryViewer, ViewingEverySpaceChangesNoState)
{
    MainWindow w(windowOptions());
    openPaused(w);
    const QByteArray before = state(w);

    Viewer v = openViewer(w);
    ASSERT_TRUE(v.table);
    for (const auto& [space, address] : {std::pair{"cpu", 0x2000u}, std::pair{"cpu", 0x4000u},
             std::pair{"ppu", 0x2000u}, std::pair{"oam", 0u}, std::pair{"palette", 0u}}) {
        goTo(v, QString::fromLatin1(space), address);
        // A few of the window's refreshes (ten a second).
        QElapsedTimer t;
        t.start();
        waitUntil([&] { return t.elapsed() > 350; });
    }
    EXPECT_EQ(state(w), before) << "the save state is unchanged";
}

TEST(GuiMemoryViewer, ShowsTheBytesSixteenToARow)
{
    MainWindow w(windowOptions());
    openPaused(w);
    QByteArray pattern;
    for (int i = 0; i < 32; ++i) pattern.append(static_cast<char>(0xA0 + i));
    call(w, "core.write_memory", {{"addr", 0x0300}, {"bytes", b64(pattern)}});

    Viewer v = openViewer(w);
    ASSERT_TRUE(v.table);
    EXPECT_EQ(v.table->columnCount(), 16);
    goTo(v, QStringLiteral("cpu"), 0x0300);
    EXPECT_TRUE(waitUntil([&] { return cell(v, 0) && cell(v, 0)->text() == QStringLiteral("A0"); }));
    EXPECT_EQ(cell(v, 17)->text(), QStringLiteral("B1"));
    EXPECT_EQ(v.table->verticalHeaderItem(1)->text(), QStringLiteral("0310"));

    // An address inside a row starts the view at that row.
    goTo(v, QStringLiteral("cpu"), 0x0315);
    EXPECT_TRUE(waitUntil([&] { return v.table->verticalHeaderItem(0)->text() == QStringLiteral("0310"); }));
}

TEST(GuiMemoryViewer, AnEditReachesEachSpace)
{
    MainWindow w(windowOptions());
    openPaused(w);
    Viewer v = openViewer(w);
    ASSERT_TRUE(v.table);
    const auto edit = [&](const char* space, unsigned address, int offset, const char* text) {
        goTo(v, QString::fromLatin1(space), address);
        ASSERT_TRUE(waitUntil([&] {
            return v.table->verticalHeaderItem(0)->text() == QStringLiteral("%1").arg(address, 4, 16, QLatin1Char('0')).toUpper()
                && cell(v, offset) != nullptr;
        }));
        cell(v, offset)->setText(QString::fromLatin1(text));
    };

    edit("cpu", 0x0400, 5, "AB");
    EXPECT_TRUE(waitUntil([&] { return bytes(call(w, "core.peek", {{"addr", 0x0405}, {"length", 1}})) == QByteArray("\xAB", 1); }));

    edit("ppu", 0x2000, 3, "$5C");
    EXPECT_TRUE(waitUntil([&] { return bytes(call(w, "core.read_vram", {{"addr", 0x2003}, {"length", 1}})) == QByteArray("\x5C", 1); }));

    edit("oam", 0, 4, "7E");
    EXPECT_TRUE(waitUntil([&] { return bytes(call(w, "core.read_oam")).at(4) == char(0x7E); }));

    edit("palette", 0, 1, "2A");
    EXPECT_TRUE(waitUntil([&] { return bytes(call(w, "core.read_palette")).at(1) == char(0x2A); }));

    // Not a byte: refused, nothing written.
    const QByteArray oam = bytes(call(w, "core.read_oam"));
    edit("oam", 0, 6, "1FF");
    QElapsedTimer t;
    t.start();
    waitUntil([&] { return t.elapsed() > 300; });
    EXPECT_EQ(bytes(call(w, "core.read_oam")), oam);
}
