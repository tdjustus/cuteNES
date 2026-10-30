// Q5 memory (tasks A016, US4).

#include "support.hpp"

using namespace cutenes::api;
using namespace cutenes::api::test;

namespace {

void loadAndRun(Session& s)
{
    ok(s, "core.load_rom", {{"path", "instr_test-v5/rom_singles/01-basics.nes"}});
    ok(s, "core.run_frames", {{"frames", 3}});
}

} // namespace

TEST(ApiMemory, RamWritesReadBack)
{
    Session s(testConfig());
    loadAndRun(s);
    ok(s, "core.write_memory", {{"addr", 0x0300}, {"bytes", b64({9, 8, 7})}});
    EXPECT_EQ(bytesOf(ok(s, "core.read_memory", {{"addr", 0x0300}, {"length", 3}})["bytes"]),
        (std::vector<std::uint8_t>{9, 8, 7}));
    // $0000-$07FF mirrors every 2 KiB (nesdev "CPU memory map").
    EXPECT_EQ(peekByte(s, 0x0B00), 9);
}

// Q5: core.peek has no side effects; core.read_memory of $2002 clears
// VBlank (nesdev "PPUSTATUS": reading clears bit 7). The flag is observed
// through core.read_ppu_registers, which reconstructs the registers without
// latch side effects. peek itself returns placeholders for the register
// space ($2000-$5FFF), like the nestest trace preview.
TEST(ApiMemory, PeekOf2002HasNoSideEffectButReadClearsVblank)
{
    Session s(testConfig());
    // The synthetic ROM only loops on JMP, so nothing but the test reads
    // $2002 (a game polling it would clear the flag itself).
    ok(s, "core.load_rom", {{"blobId", upload(s, tinyRom())}});
    ok(s, "core.run_frames", {{"frames", 3}});
    const auto vbl = [&] {
        return bytesOf(ok(s, "core.read_ppu_registers")["bytes"]).at(2) & 0x80;
    };
    // Step until VBlank is set (at most one frame of instructions).
    for (int i = 0; i < 40000 && !vbl(); ++i) {
        ok(s, "core.step_instruction");
    }
    ASSERT_EQ(vbl(), 0x80);
    // A read within a dot or two of the flag being set returns 0 and
    // suppresses it (nesdev "PPU frame timing"); step clear of that race.
    ok(s, "core.step_instruction", {{"count", 10}});
    ASSERT_EQ(vbl(), 0x80);
    ok(s, "core.peek", {{"addr", 0x2002}, {"length", 1}});
    EXPECT_EQ(vbl(), 0x80); // peek left it set
    const auto read = bytesOf(ok(s, "core.read_memory", {{"addr", 0x2002}, {"length", 1}})["bytes"]);
    EXPECT_EQ(read.at(0) & 0x80, 0x80); // the read returns the flag...
    EXPECT_EQ(vbl(), 0);                // ...and clears it
}

TEST(ApiMemory, ReadsAreBoundedAndStayInTheAddressSpace)
{
    Session s(testConfig());
    loadAndRun(s);
    EXPECT_EQ(bytesOf(ok(s, "core.peek", {{"addr", 0}, {"length", 65536}})["bytes"]).size(), 65536u);
    fails(s, "core.peek", {{"addr", 0}, {"length", 65537}}, "InvalidArguments");
    EXPECT_EQ(fails(s, "core.peek", {{"addr", 0xFFFF}, {"length", 2}}, "InvalidArguments")["path"],
        "/length");
    fails(s, "core.write_memory", {{"addr", 0}, {"bytes", b64(std::vector<std::uint8_t>(65537))}},
        "InvalidArguments");
    fails(s, "core.read_vram", {{"addr", 0x3FFF}, {"length", 2}}, "InvalidArguments");
}

TEST(ApiMemory, CpuRegisterWritesAreLimitedToTheRegisterRange)
{
    Session s(testConfig());
    loadAndRun(s);
    ok(s, "core.write_cpu_register", {{"addr", 0x2000}, {"value", 0}});
    EXPECT_EQ(fails(s, "core.write_cpu_register", {{"addr", 0x0000}, {"value", 1}}, "WriteError")["error"],
        "InvalidAddress");
    EXPECT_EQ(fails(s, "core.write_cpu_register", {{"addr", 0x4020}, {"value", 1}}, "WriteError")["error"],
        "InvalidAddress");
}
