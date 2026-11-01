// G080-G081 (spec US9-1/2, FR-060, quickstart A13): the debugger.
// nestest's reference log (tests/roms/other/nestest.log) gives every
// register after each instruction from its automation entry at $C000.
// nestest's reset vector is $C004, so the test enters at $C000 the way the
// log does, with a register edit: PC = $C000 after power-on. Expectations
// come from that log, not from the emulator.
// - An execution breakpoint stops the running game at its address, with
//   the registers the log has there, and the debugger shows it.
// - Run from a breakpoint goes on past it; Step Instruction matches the
//   log line by line; Step Frame runs one frame.
// - Registers edited in the window reach the CPU.

#include "gui_support.hpp"
#include "mainwindow.hpp"

#include <QDialog>
#include <QFile>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QRegularExpression>

#include <gtest/gtest.h>

#include <vector>

using cutenes::api::Json;
using cutenes::api::Result;
using cutenes::app::MainWindow;
using cutenes::test::waitUntil;
using cutenes::test::windowOptions;

namespace {

const QString kNestest = QStringLiteral(CUTENES_TEST_ROMS "/other/nestest.nes");

// One line of nestest.log: "C72D  EA        NOP   A:00 X:00 Y:00 P:26 SP:FB ...".
struct LogLine {
    unsigned pc = 0, a = 0, x = 0, y = 0, p = 0, s = 0;
    QString bytes; // "EA      ", the address column to the mnemonic
};

std::vector<LogLine> nestestLog()
{
    QFile file(QStringLiteral(CUTENES_TEST_ROMS "/other/nestest.log"));
    EXPECT_TRUE(file.open(QIODevice::ReadOnly | QIODevice::Text));
    const QRegularExpression re(QStringLiteral(
        "^([0-9A-F]{4})  (.{8}).*A:([0-9A-F]{2}) X:([0-9A-F]{2}) Y:([0-9A-F]{2}) P:([0-9A-F]{2}) SP:([0-9A-F]{2})"));
    std::vector<LogLine> lines;
    while (!file.atEnd()) {
        const QRegularExpressionMatch m = re.match(QString::fromLatin1(file.readLine()));
        if (!m.hasMatch()) continue;
        const auto hex = [&](int i) { return m.captured(i).toUInt(nullptr, 16); };
        lines.push_back({hex(1), hex(3), hex(4), hex(5), hex(6), hex(7), m.captured(2)});
    }
    return lines;
}

Json call(MainWindow& w, const char* name, const Json& args = Json::object())
{
    const Result r = w.session().call(name, args);
    EXPECT_FALSE(r.isError()) << name << ": " << (r.isError() ? r.error->message : "");
    return r.value;
}

Json cpu(MainWindow& w)
{
    return call(w, "core.get_cpu")["cpu"];
}

// The registers equal the log's line (before that line's instruction runs).
void expectAt(MainWindow& w, const LogLine& line)
{
    const Json c = cpu(w);
    EXPECT_EQ(c["pc"], line.pc) << std::hex << "pc, log line at " << line.pc;
    EXPECT_EQ(c["a"], line.a) << std::hex << "a at " << line.pc;
    EXPECT_EQ(c["x"], line.x) << std::hex << "x at " << line.pc;
    EXPECT_EQ(c["y"], line.y) << std::hex << "y at " << line.pc;
    EXPECT_EQ(c["p"], line.p) << std::hex << "p at " << line.pc;
    EXPECT_EQ(c["s"], line.s) << std::hex << "s at " << line.pc;
    const Json dis = call(w, "core.disassemble", {{"addr", line.pc}, {"count", 1}});
    EXPECT_EQ(QString::fromStdString(dis["lines"][0]["text"].get<std::string>()).mid(6, 8), line.bytes)
        << "the instruction's bytes at " << std::hex << line.pc;
}

std::size_t indexOf(const std::vector<LogLine>& log, unsigned pc)
{
    for (std::size_t i = 0; i < log.size(); ++i) {
        if (log[i].pc == pc) return i;
    }
    ADD_FAILURE() << "no log line at " << std::hex << pc;
    return 0;
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

bool pausedAt(MainWindow& w, unsigned pc)
{
    return call(w, "core.run_state")["runState"] == "Paused" && cpu(w)["pc"] == pc;
}

} // namespace

TEST(GuiDebugger, ABreakpointStopsTheRunningGameWithTheLogsRegisters)
{
    const std::vector<LogLine> log = nestestLog();
    ASSERT_GT(log.size(), 5000u);
    MainWindow w(windowOptions());
    enterNestest(w);
    expectAt(w, log[0]);

    // $C72D: the JSR target of log line 6.
    const std::size_t at = indexOf(log, 0xC72D);
    call(w, "gui.add_breakpoint", {{"address", 0xC72D}});
    EXPECT_EQ(call(w, "gui.list_breakpoints")["breakpoints"], Json::array({0xC72D}));
    EXPECT_EQ(call(w, "gui.debug_run")["runState"], "Running");
    ASSERT_TRUE(waitUntil([&] { return pausedAt(w, 0xC72D); })) << "stopped at the breakpoint";
    expectAt(w, log[at]);

    // The debugger shows it.
    QDialog* debugger = nullptr;
    ASSERT_TRUE(waitUntil([&] {
        debugger = w.findChild<QDialog*>(QStringLiteral("debuggerWindow"));
        return debugger && debugger->isVisible();
    }));
    auto* pc = debugger->findChild<QLineEdit*>(QStringLiteral("registerPC"));
    ASSERT_TRUE(pc);
    EXPECT_TRUE(waitUntil([&] { return pc->text().toUpper() == QStringLiteral("C72D"); })) << pc->text().toStdString();
    auto* listing = debugger->findChild<QListWidget*>(QStringLiteral("disassembly"));
    ASSERT_TRUE(listing);
    EXPECT_TRUE(waitUntil([&] { return listing->count() > 0 && listing->item(0)->data(Qt::UserRole).toUInt() == 0xC72D
            && listing->item(0)->text().contains(QStringLiteral("C72D  EA")) && listing->item(0)->text().contains(QStringLiteral("●")); }))
        << "the listing starts at PC, marked as a breakpoint";

    // Run goes on from the breakpoint rather than stopping there again.
    const unsigned next = log[at + 1].pc;
    call(w, "gui.add_breakpoint", {{"address", next}});
    call(w, "gui.debug_run");
    ASSERT_TRUE(waitUntil([&] { return pausedAt(w, next); }));
    expectAt(w, log[at + 1]);

    call(w, "gui.remove_breakpoint", {{"address", 0xC72D}});
    EXPECT_EQ(call(w, "gui.list_breakpoints")["breakpoints"], Json::array({next}));
    EXPECT_TRUE(w.session().call("gui.remove_breakpoint", {{"address", 0x1234}}).isError()) << "no such breakpoint";
}

TEST(GuiDebugger, StepInstructionMatchesTheLogForTenSteps)
{
    const std::vector<LogLine> log = nestestLog();
    MainWindow w(windowOptions());
    enterNestest(w);
    for (std::size_t i = 1; i <= 10; ++i) {
        const Json stepped = call(w, "gui.debug_step_instruction");
        EXPECT_EQ(stepped["cpu"]["pc"], log[i].pc);
        expectAt(w, log[i]);
    }
    const Json many = call(w, "gui.debug_step_instruction", {{"count", 5}});
    EXPECT_EQ(many["cpu"]["pc"], log[15].pc);
    EXPECT_EQ(call(w, "core.run_state")["runState"], "Paused");
}

TEST(GuiDebugger, StepFrameRunsOneFrameAndBreakpointsStopIt)
{
    const std::vector<LogLine> log = nestestLog();
    MainWindow w(windowOptions());
    enterNestest(w);
    const auto frame = [&] { return call(w, "core.run_state")["frame"].get<std::uint64_t>(); };
    const std::uint64_t before = frame();
    call(w, "gui.debug_step_frame");
    EXPECT_EQ(frame(), before + 1);
    EXPECT_EQ(call(w, "core.run_state")["runState"], "Paused");

    // A breakpoint inside the frame ends the step there.
    call(w, "core.power_on");
    call(w, "core.set_cpu", {{"pc", 0xC000}});
    call(w, "gui.add_breakpoint", {{"address", 0xC72D}});
    const Json stopped = call(w, "gui.debug_step_frame");
    EXPECT_EQ(stopped["breakpoint"], 0xC72D);
    expectAt(w, log[indexOf(log, 0xC72D)]);
    // The rest of the frame: on to the frame's end, no second stop there.
    const std::uint64_t mid = frame();
    call(w, "gui.debug_step_frame");
    EXPECT_EQ(frame(), mid + 1);
}

TEST(GuiDebugger, RegistersEditedInTheWindowReachTheCpu)
{
    MainWindow w(windowOptions());
    enterNestest(w);
    call(w, "gui.show_debugger");
    QDialog* debugger = nullptr;
    ASSERT_TRUE(waitUntil([&] {
        debugger = w.findChild<QDialog*>(QStringLiteral("debuggerWindow"));
        return debugger && debugger->isVisible();
    }));
    auto* a = debugger->findChild<QLineEdit*>(QStringLiteral("registerA"));
    auto* x = debugger->findChild<QLineEdit*>(QStringLiteral("registerX"));
    ASSERT_TRUE(a && x);
    ASSERT_TRUE(waitUntil([&] { return a->text() == QStringLiteral("00"); }));
    // As typed: setText, then the edit counts as the user's.
    a->setText(QStringLiteral("5A"));
    a->setModified(true);
    emit a->editingFinished();
    x->setText(QStringLiteral("$7f"));
    x->setModified(true);
    emit x->editingFinished();
    EXPECT_TRUE(waitUntil([&] { return cpu(w)["a"] == 0x5A && cpu(w)["x"] == 0x7F; })) << cpu(w).dump();

    // The step buttons drive the same commands.
    auto* step = debugger->findChild<QPushButton*>(QStringLiteral("stepInstruction"));
    ASSERT_TRUE(step);
    step->click();
    EXPECT_TRUE(waitUntil([&] { return cpu(w)["pc"] == 0xC5F5; })) << "JMP $C5F5 ran";
}
