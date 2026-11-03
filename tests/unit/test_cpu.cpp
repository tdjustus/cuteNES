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

// --- NMI sampling (issue #42) ----------------------------------------------
// nesdev "CPU interrupts": the edge detector "polls the status of the NMI
// line during φ2 of each CPU cycle (i.e., during the second half of each
// cycle)". nesdev "PPU frame timing": a $2002 read on the set dot clears the
// flag, and "the $2002 read pulling the NMI line back up too quickly after it
// drops" means no NMI. So the line is sampled once per cycle, at the end of
// φ2, and a level that comes and goes between two samples is never seen.
namespace {

// A PPU stand-in whose NMI line rises at a chosen master time and drops on a
// $2002 read or a $2000 write.
struct FakeNmiPpu final : cutenes::CpuBusDevice, cutenes::Tickable {
    std::uint64_t riseAt = ~0ull;
    std::uint64_t now = 0;
    bool dropped = false;

    void runTo(std::uint64_t masterClock) override { now = masterClock; }
    bool nmiLine() const override { return now >= riseAt && !dropped; }
    std::uint8_t cpuRead(std::uint16_t addr) override
    {
        const bool high = nmiLine();
        if (addr == 0x2002) {
            dropped = true;
        }
        return high ? 0x80 : 0x00;
    }
    void cpuWrite(std::uint16_t addr, std::uint8_t) override
    {
        if (addr == 0x2000) {
            dropped = true;
        }
    }
};

struct NmiHarness {
    cutenes::Scheduler scheduler{cutenes::regionProfile(cutenes::Region::Ntsc)};
    cutenes::Bus bus;
    FakePrg prg;
    FakeNmiPpu ppu;
    cutenes::Cpu cpu{bus, scheduler, &ppu, nullptr};

    // Runs one 4-cycle instruction at $8000 whose last cycle accesses the
    // PPU (cycles 0-12, 12-24, 24-36 and 36-48 in master ticks), then one
    // more step. Returns true if that step took the NMI.
    bool nmiAfter(std::initializer_list<std::uint8_t> instruction,
                  std::uint64_t riseAt)
    {
        bus.attachCartridge(&prg);
        bus.attachPpu(&ppu);
        prg.prg[(0xFFFA - 0x8000) & 0x3FFF] = 0x00;
        prg.prg[(0xFFFB - 0x8000) & 0x3FFF] = 0x90;
        std::size_t i = 0;
        for (const std::uint8_t b : instruction) {
            prg.prg[i++] = b;
        }
        prg.prg[i] = 0xEA; // NOP
        cpu.reset(/*cold=*/true);
        cpu.restartClockForTest();
        cpu.setProgramCounter(0x8000);
        ppu.riseAt = riseAt;
        cpu.step();
        cpu.step();
        return cpu.state().pc == 0x9000;
    }
};

constexpr std::initializer_list<std::uint8_t> kLda2002{0xAD, 0x02, 0x20};
constexpr std::initializer_list<std::uint8_t> kSta2000{0x8D, 0x00, 0x20};

} // namespace

// High through the end of cycle 3, dropped by the write in cycle 4: the
// cycle-3 sample sees it, so the NMI is taken after the STA.
TEST(CpuNmiSampling, ALineHighAtTheEndOfACycleIsLatched)
{
    NmiHarness h;
    EXPECT_TRUE(h.nmiAfter(kSta2000, 32));
}

// Rises in cycle 4 before the $2002 read, which drops it before cycle 4's
// sample: no sample ever sees it high (rule 3).
TEST(CpuNmiSampling, AReadThatDropsTheLineBeforeTheNextSampleHidesTheNmi)
{
    NmiHarness h;
    EXPECT_FALSE(h.nmiAfter(kLda2002, 38));
}

// Rises in cycle 3: the cycle-3 sample latches it before the read drops it.
TEST(CpuNmiSampling, AReadAfterTheSampleDoesNotCancelTheNmi)
{
    NmiHarness h;
    EXPECT_TRUE(h.nmiAfter(kLda2002, 32));
}
