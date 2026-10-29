#include "cutenes/cpu/cpu.hpp"

#include "cutenes/bus/bus.hpp"
#include "cutenes/system/scheduler.hpp"

#include <cutenes/cutenes.hpp>

#include <gtest/gtest.h>

namespace {

// Cartridge-slot stand-in so vector fetches and program reads return real
// bytes: mirrors a 16K PRG window.
struct FakePrg final : cutenes::CpuBusDevice {
    std::uint8_t prg[16384]{};
    std::uint8_t cpuRead(std::uint16_t addr) override
    {
        return prg[(addr - 0x8000) & 0x3FFF];
    }
    void cpuWrite(std::uint16_t, std::uint8_t) override {}
};

struct CpuHarness {
    cutenes::Scheduler scheduler{cutenes::regionProfile(cutenes::Region::Ntsc)};
    cutenes::Bus bus;
    FakePrg prg;
    cutenes::Cpu cpu{bus, scheduler, nullptr, nullptr};

    CpuHarness()
    {
        bus.attachCartridge(&prg);
        cpu.reset(/*cold=*/true);
        // Rebase for easy tick math: reset consumed its 7 cycles.
        cpu.restartClockForTest();
    }

    // Assemble tiny programs at $8000 (word-addressable helper).
    void setPrg(std::uint16_t addr, std::initializer_list<std::uint8_t> bytes)
    {
        std::size_t i = 0;
        for (const std::uint8_t b : bytes) {
            prg.prg[((addr - 0x8000) + i) & 0x3FFF] = b;
            ++i;
        }
    }

    std::uint64_t runInstruction()
    {
        const std::uint64_t before = scheduler.now();
        cpu.step();
        return scheduler.now() - before;
    }
};

constexpr std::uint64_t kTick = 12; // master ticks per CPU cycle (NTSC)

} // namespace

TEST(CpuTable, All256OpcodesAreDefined)
{
    for (int op = 0; op < 256; ++op) {
        const auto& info = cutenes::Cpu::opcodeInfo(
            static_cast<std::uint8_t>(op));
        EXPECT_GE(info.cycles, 2u) << "opcode " << std::hex << op;
    }
}

TEST(Cpu, LdaImmediateTakesTwoCycles)
{
    CpuHarness h;
    h.setPrg(0x8000, {0xA9, 0x05});
    h.cpu.setProgramCounter(0x8000);
    h.cpu.step();
    EXPECT_EQ(h.cpu.state().a, 0x05);
    EXPECT_EQ(h.scheduler.now(), 2 * kTick);
    EXPECT_EQ(h.cpu.state().pc, 0x8002);
}

TEST(Cpu, FlagsNAndZFollowLoadedValue)
{
    CpuHarness h;
    h.setPrg(0x8000, {0xA9, 0x80});
    h.cpu.setProgramCounter(0x8000);
    h.cpu.step();
    EXPECT_NE(h.cpu.state().p & 0x80, 0); // N

    h.setPrg(0x8002, {0xA9, 0x00});
    h.cpu.step();
    EXPECT_NE(h.cpu.state().p & 0x02, 0); // Z
}

TEST(Cpu, AbsXReadPageCrossAddsACycle)
{
    CpuHarness h;
    h.setPrg(0x8000, {0xA2, 0x01,      // LDX #$01
                      0xBD, 0xFF, 0x12}); // LDA $12FF,X -> $1300
    // Put $77 at $1300 via RAM mirroring ($1300 is RAM).
    h.bus.write(0x1300, 0x77);
    h.cpu.setProgramCounter(0x8000);
    h.cpu.step(); // LDX
    EXPECT_EQ(h.runInstruction(), 5 * kTick); // page cross: 4+1
    EXPECT_EQ(h.cpu.state().a, 0x77);
}

TEST(Cpu, AbsXReadSamePageIsFourCycles)
{
    CpuHarness h;
    h.setPrg(0x8000, {0xA2, 0x01,         // LDX #$01
                      0xBD, 0x00, 0x12}); // LDA $1200,X -> $1201
    h.bus.write(0x1201, 0x33);
    h.cpu.setProgramCounter(0x8000);
    h.cpu.step();
    EXPECT_EQ(h.runInstruction(), 4 * kTick);
    EXPECT_EQ(h.cpu.state().a, 0x33);
}

TEST(Cpu, StaAbsXAlwaysFiveCycles)
{
    CpuHarness h;
    h.setPrg(0x8000, {0xA2, 0x01,         // LDX #$01
                      0x9D, 0x00, 0x12}); // STA $1200,X
    h.cpu.setProgramCounter(0x8000);
    h.cpu.step();
    EXPECT_EQ(h.runInstruction(), 5 * kTick); // no page-cross penalty on stores
    EXPECT_EQ(h.bus.read(0x1201), 0x00); // placeholder; store wrote A (0)
}

TEST(Cpu, JmpIndirectWrapsWithinPage)
{
    CpuHarness h;
    // Vector at $10FF/$1000 wraps within the page (NMOS bug). $10FF lives in
    // the RAM mirror window, so both bytes are directly settable.
    h.setPrg(0x8000, {0x6C, 0xFF, 0x10});
    h.cpu.setProgramCounter(0x8000);
    h.bus.write(0x10FF, 0x34); // low byte at the vector address
    h.bus.write(0x1000, 0x12); // high byte wraps to the page base
    h.cpu.step();
    EXPECT_EQ(h.cpu.state().pc, 0x1234);
}

TEST(Cpu, RmwIncZpgIsFiveCyclesAndWritesResult)
{
    CpuHarness h;
    h.setPrg(0x8000, {0xE6, 0x80}); // INC $80
    h.bus.write(0x80, 0x41);
    h.cpu.setProgramCounter(0x8000);
    EXPECT_EQ(h.runInstruction(), 5 * kTick);
    EXPECT_EQ(h.bus.read(0x80), 0x42);
}

TEST(Cpu, BranchNotTakenTwoCyclesTakenThree)
{
    CpuHarness h;
    h.setPrg(0x8000, {0xA9, 0x01,    // LDA #$01 -> Z clear
                      0xD0, 0x05});  // BNE +5
    h.cpu.setProgramCounter(0x8000);
    h.cpu.step();
    EXPECT_EQ(h.runInstruction(), 3 * kTick); // taken, same page
    EXPECT_EQ(h.cpu.state().pc, 0x8009);

    h.setPrg(0x8002, {0xA9, 0x00,    // LDA #$00 -> Z set
                      0xD0, 0x05});  // BNE +5 (not taken)
    h.cpu.setProgramCounter(0x8002);
    h.cpu.step();
    EXPECT_EQ(h.runInstruction(), 2 * kTick);
}

TEST(Cpu, NmiPushesStateAndJumpsToVector)
{
    CpuHarness h;
    // NMI vector $FFFA/$FFFB -> $C002.
    h.prg.prg[(0xFFFA - 0x8000) & 0x3FFF] = 0x02;
    h.prg.prg[(0xFFFB - 0x8000) & 0x3FFF] = 0xC0;
    h.cpu.setProgramCounter(0x8000);

    // NMI asserted before the boundary is serviced at that boundary — the
    // interrupt sequence is this step's entire cycle work (7 cycles).
    h.cpu.assertNmi();
    h.cpu.step();
    EXPECT_EQ(h.cpu.state().pc, 0xC002);
    EXPECT_EQ(h.scheduler.now(), 7 * kTick);
    EXPECT_EQ(h.cpu.state().s, 0xFA); // $FD minus three pushes

    h.setPrg(0xC002, {0xEA});
    h.cpu.step(); // next step executes the handler's first instruction
    EXPECT_EQ(h.cpu.state().pc, 0xC003);
    EXPECT_EQ(h.scheduler.now(), 7 * kTick + 2 * kTick);
}

// Branch Timing Tests 3 (blargg, passes on hardware): a taken branch that does
// not cross a page delays only the IRQ poll, not NMI. An NMI that rose before
// the branch's last cycle is serviced at the next boundary, before the next
// instruction.
TEST(Cpu, NmiRaisedDuringTakenBranchIsServicedAtNextBoundary)
{
    CpuHarness h;
    h.prg.prg[(0xFFFA - 0x8000) & 0x3FFF] = 0x02;
    h.prg.prg[(0xFFFB - 0x8000) & 0x3FFF] = 0xC0;
    h.setPrg(0x8000, {0xD0, 0x01,    // BNE +1 (Z clear at reset: taken)
                      0xEA, 0xEA});
    h.cpu.setProgramCounter(0x8000);
    h.cpu.step(); // the taken branch
    EXPECT_EQ(h.cpu.state().pc, 0x8003);
    h.cpu.assertNmi(); // rose during the branch
    h.cpu.step();
    EXPECT_EQ(h.cpu.state().pc, 0xC002); // serviced, NOP at $8003 not run
}

TEST(Cpu, ResetStateMatchesPowerOnConstants)
{
    CpuHarness h;
    const auto s = h.cpu.state();
    EXPECT_EQ(s.a, 0);
    EXPECT_EQ(s.x, 0);
    EXPECT_EQ(s.y, 0);
    EXPECT_EQ(s.s, 0xFD);
    EXPECT_EQ(s.p, 0x24); // I set, bit 5 reads 1, no B flag
}

TEST(Cpu, UnofficialLaxLoadsAAndX)
{
    CpuHarness h;
    h.setPrg(0x8000, {0xA7, 0x40}); // LAX $40
    h.bus.write(0x40, 0x99);
    h.cpu.setProgramCounter(0x8000);
    EXPECT_EQ(h.runInstruction(), 3 * kTick);
    EXPECT_EQ(h.cpu.state().a, 0x99);
    EXPECT_EQ(h.cpu.state().x, 0x99);
}

TEST(Cpu, UnofficialSloShiftsMemoryThenOra)
{
    CpuHarness h;
    h.setPrg(0x8000, {0xA9, 0x0F,    // LDA #$0F
                      0x07, 0x40});  // SLO $40
    h.bus.write(0x40, 0x81);
    h.cpu.setProgramCounter(0x8000);
    h.cpu.step();
    EXPECT_EQ(h.runInstruction(), 5 * kTick);
    EXPECT_EQ(h.bus.read(0x40), 0x02); // 0x81 << 1 = 0x02, C set
    EXPECT_EQ(h.cpu.state().a, 0x0F | 0x02);
    EXPECT_NE(h.cpu.state().p & 0x01, 0); // C from the shift
}
