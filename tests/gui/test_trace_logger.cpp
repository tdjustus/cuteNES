// G084 (spec US9-5, FR-063, FR-064): the trace logger and Write Memory Dump.
// - The trace is the project's trace format, nestest.log's: from the log's
//   entry ($C000, as the debugger tests enter it) the file equals the log
//   line for line, cycle counts and PPU positions included. Expectations
//   come from that log, not from the emulator.
// - Without a path the trace goes to the library's debug folder as
//   trace-<YYYYMMDD-HHMMSS>.log (contracts/library-layout.md); it logs
//   while the game runs, and closing the game ends it with the file whole.
// - The trace logger window starts and stops it and shows the file; a
//   core.trace in between does not end it.
// - Write Memory Dump writes <YYYYMMDDHHMMSS>.dmp into the debug folder in
//   the core's documented layout (001 core-api.md "Memory dump").

#include "gui_support.hpp"
#include "mainwindow.hpp"

#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QPushButton>
#include <QRegularExpression>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>

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

QStringList lines(const QString& path)
{
    QFile file(path);
    EXPECT_TRUE(file.open(QIODevice::ReadOnly | QIODevice::Text)) << path.toStdString();
    QStringList out;
    while (!file.atEnd()) {
        QString line = QString::fromLatin1(file.readLine());
        while (line.endsWith(QLatin1Char('\n')) || line.endsWith(QLatin1Char('\r'))) line.chop(1);
        out << line;
    }
    return out;
}

// nestest from the log's entry: powered on, paused, PC = $C000.
void enterNestest(MainWindow& w)
{
    w.show();
    call(w, "gui.set_setting", {{"key", "emulation.pauseWhenInactive"}, {"value", false}});
    call(w, "gui.open_rom", {{"path", kNestest.toStdString()}});
    call(w, "core.power_on"); // leaves the machine paused
    call(w, "core.set_cpu", {{"pc", 0xC000}});
}

QString debugFolder(const QString& folder)
{
    return QDir(folder).filePath(QStringLiteral("library/debug"));
}

// The bytes of a blob, read through session.blob_read.
QByteArray blob(MainWindow& w, const Json& handle)
{
    QByteArray out;
    const auto size = handle["size"].get<std::int64_t>();
    while (out.size() < size) {
        const Json part = call(w, "session.blob_read",
            {{"blobId", handle["blobId"]}, {"offset", out.size()}, {"length", 65536}});
        out += QByteArray::fromBase64(QByteArray::fromStdString(part["bytes"].get<std::string>()));
        if (part["eof"].get<bool>()) break;
    }
    return out;
}

std::uint64_t le64(const QByteArray& bytes, int at)
{
    std::uint64_t v = 0;
    for (int i = 7; i >= 0; --i) v = v << 8 | static_cast<unsigned char>(bytes[at + i]);
    return v;
}

} // namespace

TEST(GuiTraceLogger, TheTraceIsNestestsLogLineForLine)
{
    const QStringList log = lines(QStringLiteral(CUTENES_TEST_ROMS "/other/nestest.log"));
    ASSERT_GT(log.size(), 5000);
    MainWindow w(windowOptions());
    enterNestest(w);

    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("nestest.log"));
    const Json started = call(w, "gui.trace_start", {{"path", path.toStdString()}});
    EXPECT_EQ(started["path"], path.toStdString());
    EXPECT_EQ(call(w, "gui.trace_status")["active"], true);
    // Starting twice is refused: one trace at a time.
    EXPECT_EQ(w.session().call("gui.trace_start", Json::object()).error->name, "Tracing");

    constexpr int kSteps = 3000;
    call(w, "gui.debug_step_instruction", {{"count", kSteps}});
    EXPECT_EQ(call(w, "gui.trace_status")["lines"], kSteps);
    const Json stopped = call(w, "gui.trace_stop");
    EXPECT_EQ(stopped["path"], path.toStdString());
    EXPECT_EQ(stopped["lines"], kSteps);
    EXPECT_EQ(call(w, "gui.trace_status")["active"], false);
    EXPECT_EQ(w.session().call("gui.trace_stop", Json::object()).error->name, "NotTracing");

    const QStringList traced = lines(path);
    ASSERT_EQ(traced.size(), kSteps);
    for (int i = 0; i < kSteps; ++i) ASSERT_EQ(traced[i], log[i]) << "line " << i + 1;

    // Stopped, instructions are no longer logged.
    call(w, "gui.debug_step_instruction", {{"count", 10}});
    EXPECT_EQ(lines(path).size(), kSteps);
}

TEST(GuiTraceLogger, ARunningTraceGoesToTheDebugFolderAndEndsWithTheGame)
{
    QString folder;
    MainWindow w(windowOptions(&folder));
    enterNestest(w);

    const QString path = QString::fromStdString(call(w, "gui.trace_start")["path"].get<std::string>());
    EXPECT_EQ(QFileInfo(path).absolutePath(), QFileInfo(debugFolder(folder)).absoluteFilePath());
    EXPECT_TRUE(QRegularExpression(QStringLiteral("^trace-\\d{8}-\\d{6}(-\\d+)?\\.log$"))
                    .match(QFileInfo(path).fileName())
                    .hasMatch())
        << path.toStdString();

    call(w, "core.resume");
    ASSERT_TRUE(waitUntil([&] { return call(w, "gui.trace_status")["lines"].get<std::int64_t>() > 50000; }));
    // Closing the game ends the trace with every line written.
    call(w, "gui.close_game");
    EXPECT_EQ(call(w, "gui.trace_status")["active"], false);
    const QStringList traced = lines(path);
    ASSERT_GT(traced.size(), 50000);
    // Each line is the trace format: address, bytes, instruction, registers,
    // PPU position and cycle count.
    const QRegularExpression format(QStringLiteral(
        "^[0-9A-F]{4}  [0-9A-F ]{9}[ *][A-Z]{3} .* A:[0-9A-F]{2} X:[0-9A-F]{2} Y:[0-9A-F]{2} P:[0-9A-F]{2} "
        "SP:[0-9A-F]{2} PPU:[ \\d]{3},[ \\d]{3} CYC:\\d+$"));
    for (const QString& line : {traced.first(), traced[traced.size() / 2], traced.last()}) {
        EXPECT_TRUE(format.match(line).hasMatch()) << line.toStdString();
    }
}

TEST(GuiTraceLogger, TheWindowStartsAndStopsTheTrace)
{
    QString folder;
    MainWindow w(windowOptions(&folder));
    enterNestest(w);
    call(w, "gui.show_trace_logger");
    QDialog* window = nullptr;
    ASSERT_TRUE(waitUntil([&] {
        window = w.findChild<QDialog*>(QStringLiteral("traceLogger"));
        return window && window->isVisible();
    }));
    auto* toggle = window->findChild<QPushButton*>(QStringLiteral("traceToggle"));
    auto* file = window->findChild<QLabel*>(QStringLiteral("traceFile"));
    auto* count = window->findChild<QLabel*>(QStringLiteral("traceLines"));
    ASSERT_TRUE(toggle && file && count);
    EXPECT_EQ(toggle->text(), QStringLiteral("Start Logging"));

    toggle->click();
    ASSERT_TRUE(waitUntil([&] { return call(w, "gui.trace_status")["active"] == true; }));
    const QString path = QString::fromStdString(call(w, "gui.trace_status")["path"].get<std::string>());
    EXPECT_TRUE(waitUntil([&] { return toggle->text() == QStringLiteral("Stop Logging"); }));
    EXPECT_TRUE(waitUntil([&] { return file->text().contains(QFileInfo(path).fileName()); })) << file->text().toStdString();

    call(w, "gui.debug_step_instruction", {{"count", 25}});
    EXPECT_TRUE(waitUntil([&] { return count->text() == QStringLiteral("25 instructions"); })) << count->text().toStdString();

    // core.trace borrows the core's one trace callback and turns it off when
    // done; the logger takes it back, so later instructions are logged
    // (core.trace's own are in its result, not the file).
    call(w, "core.trace", {{"instructions", 3}});
    call(w, "gui.debug_step_instruction", {{"count", 5}});
    EXPECT_EQ(call(w, "gui.trace_status")["lines"], 30);

    toggle->click();
    ASSERT_TRUE(waitUntil([&] { return call(w, "gui.trace_status")["active"] == false; }));
    EXPECT_TRUE(waitUntil([&] { return toggle->text() == QStringLiteral("Start Logging"); }));
    EXPECT_EQ(lines(path).size(), 30);
}

TEST(GuiTraceLogger, WriteMemoryDumpWritesTheCoresDumpToTheDebugFolder)
{
    QString folder;
    MainWindow w(windowOptions(&folder));
    enterNestest(w);
    call(w, "gui.debug_step_instruction", {{"count", 1000}});
    const std::int64_t before = QDateTime::currentSecsSinceEpoch();

    const QString path = QString::fromStdString(call(w, "gui.write_memory_dump")["path"].get<std::string>());
    EXPECT_EQ(QFileInfo(path).absolutePath(), QFileInfo(debugFolder(folder)).absoluteFilePath());
    EXPECT_TRUE(QRegularExpression(QStringLiteral("^\\d{14}(-\\d+)?\\.dmp$")).match(QFileInfo(path).fileName()).hasMatch())
        << path.toStdString();
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    const QByteArray dump = file.readAll();

    // The documented layout: magic, timestamp (Unix seconds), frame counter
    // and master cycle, then CPU memory and the rest.
    ASSERT_GT(dump.size(), 32 + 65536);
    EXPECT_EQ(dump.left(8), QByteArray("CUNEDMP1"));
    const auto stamp = static_cast<std::int64_t>(le64(dump, 8));
    EXPECT_GE(stamp, before);
    EXPECT_LE(stamp, QDateTime::currentSecsSinceEpoch());
    const Json counters = call(w, "core.counters");
    EXPECT_EQ(le64(dump, 16), counters["frame"].get<std::uint64_t>());
    EXPECT_EQ(le64(dump, 24), counters["masterCycle"].get<std::uint64_t>());
    // Byte for byte the core's dump of the same moment, but for the time.
    QByteArray core = blob(w, call(w, "core.dump_memory"));
    ASSERT_EQ(core.size(), dump.size());
    std::memcpy(core.data() + 8, dump.constData() + 8, 8);
    EXPECT_EQ(core, dump);
}
