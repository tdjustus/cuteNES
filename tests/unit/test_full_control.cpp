// Full-control API surface tests (task T067; contract amendment 2026-10-18).
// Covers: writeCpu mask semantics, side-effect-free PPU debug access,
// peek without latch side effects, OAM/palette round trips with alias
// resolution, scripted-input determinism, and disassembly.

#include <cutenes/cutenes.hpp>

#include <gtest/gtest.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

// Minimal NROM-128 image with a controller-echo program:
//   $C000: strobe on/off, then loop { LDA $4016; STA $10; JMP }
// The first controller read returns button A.
constexpr std::size_t kPrg = 16384;
std::vector<std::uint8_t> makeEchoRom()
{
    std::vector<std::uint8_t> rom{
        'N', 'E', 'S', 0x1A, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    rom.resize(16 + kPrg, 0);
    const std::size_t base = 16;
    const std::uint8_t prog[] = {
        0xA9, 0x01, 0x8D, 0x16, 0x40, // LDA #$01; STA $4016 (strobe on)
        0xA9, 0x00, 0x8D, 0x16, 0x40, // LDA #$00; STA $4016 (latch)
        0xAD, 0x16, 0x40,             // loop: LDA $4016 (button A)
        0x85, 0x10,                   // STA $10
        0x4C, 0x00, 0xC0,             // JMP $C000 (re-strobe each pass)
    };
    std::memcpy(rom.data() + base, prog, sizeof prog);
    rom[base + 0x3FFC] = 0x00; // reset -> $C000
    rom[base + 0x3FFD] = 0xC0;
    return rom;
}

struct FullControlHarness {
    std::vector<std::uint8_t> rom = makeEchoRom();
    cutenes::Machine machine;
    cutenes::ButtonSet none;

    FullControlHarness()
    {
        const auto result =
            machine.loadCartridge(rom.data(), rom.size());
        EXPECT_EQ(result.error, cutenes::LoadError::Ok);
        machine.powerOn();
    }
};

} // namespace

TEST(WriteCpu, RoundTripAndMaskSelectivity)
{
    FullControlHarness h;
    cutenes::CpuWrite w{};
    w.a = 0x12;
    w.x = 0x34;
    w.y = 0x56;
    w.s = 0x80;
    w.p = 0xA5; // bit 5 must be forced on; B (bit 4) never materialized
    w.pc = 0xBEEF;
    w.mask = 0x3F; // all six fields
    h.machine.writeCpu(w);

    const auto cpu = h.machine.cpu();
    EXPECT_EQ(cpu.a, 0x12);
    EXPECT_EQ(cpu.x, 0x34);
    EXPECT_EQ(cpu.y, 0x56);
    EXPECT_EQ(cpu.s, 0x80);
    EXPECT_EQ(cpu.pc, 0xBEEF);
    EXPECT_EQ(cpu.p & 0x20, 0x20); // bit 5 forced
    EXPECT_EQ(cpu.p & 0x10, 0x00); // B never materialized
    EXPECT_EQ(cpu.p & 0x80, 0x80); // N from 0xA5 preserved

    // Mask: write only A; other fields untouched.
    cutenes::CpuWrite partial{};
    partial.a = 0xFF;
    partial.pc = 0x1111;
    partial.mask = 0x01; // a only
    h.machine.writeCpu(partial);
    const auto cpu2 = h.machine.cpu();
    EXPECT_EQ(cpu2.a, 0xFF);
    EXPECT_EQ(cpu2.x, 0x34); // untouched
    EXPECT_EQ(cpu2.pc, 0xBEEF); // untouched
}

TEST(DebugAccess, PeekHasNoSideEffects)
{
    FullControlHarness h;
    h.machine.runFrames(2); // enter/leave vblank; flag was set mid-frame
    // Advance into vblank: run one more partial... simpler: poke RAM.
    const std::uint8_t one = 1;
    h.machine.writeMemory(0x0100, &one, 1);
    const std::uint8_t first = h.machine.peek(0x0100);
    const std::uint8_t second = h.machine.peek(0x0100);
    EXPECT_EQ(first, 1);
    EXPECT_EQ(second, 1);
    // Repeated peeks of $2002 must not clear VBlank: run deep into a frame
    // so the flag state is whatever it is, then peek twice and compare.
    const std::uint8_t s1 = h.machine.peek(0x2002);
    const std::uint8_t s2 = h.machine.peek(0x2002);
    EXPECT_EQ(s1, s2); // identical => no read-side-effect
    // peek must not advance time.
    const auto before = h.machine.masterCycle();
    (void)h.machine.peek(0x0500);
    EXPECT_EQ(h.machine.masterCycle(), before);
}

TEST(DebugAccess, VramRoundTripAndMirroring)
{
    FullControlHarness h;
    std::uint8_t pattern[64];
    for (int i = 0; i < 64; ++i) {
        pattern[i] = static_cast<std::uint8_t>(0xC0 ^ i);
    }
    h.machine.writeVram(0x2000, pattern, sizeof pattern);

    std::uint8_t back[64] = {};
    h.machine.readVram(0x2000, sizeof back, back);
    EXPECT_EQ(0, std::memcmp(pattern, back, sizeof pattern));

    // Horizontal mirroring (this ROM's default): $2400 mirrors $2000.
    std::uint8_t mirror[16] = {};
    h.machine.readVram(0x2400, sizeof mirror, mirror);
    EXPECT_EQ(0, std::memcmp(pattern, mirror, sizeof mirror));

    // Debug access must not touch the $2006/$2007 pipeline: the v latch
    // is unobservable, but time must not move.
    const auto before = h.machine.masterCycle();
    std::uint8_t sink[16] = {};
    h.machine.readVram(0x23F0, sizeof sink, sink);
    EXPECT_EQ(h.machine.masterCycle(), before);
}

TEST(DebugAccess, OamAndPaletteRoundTrip)
{
    FullControlHarness h;
    std::uint8_t oam[256];
    for (int i = 0; i < 256; ++i) {
        oam[i] = static_cast<std::uint8_t>(i * 3);
    }
    h.machine.writeOam(oam);
    std::uint8_t oamBack[256] = {};
    h.machine.readOam(oamBack);
    EXPECT_EQ(0, std::memcmp(oam, oamBack, sizeof oam));

    std::uint8_t pal[32];
    for (int i = 0; i < 32; ++i) {
        pal[i] = static_cast<std::uint8_t>(0x10 + i);
    }
    h.machine.writePalette(pal);
    std::uint8_t palBack[32] = {};
    h.machine.readPalette(palBack);
    // Alias resolution: indices 0x00/0x04/... and 0x10/0x14/... share one
    // backing entry; the later write (the sprite alias) wins, and both
    // aliases read it back.
    for (int i = 0; i < 32; ++i) {
        const std::uint8_t expected = (i & 3) == 0
            ? pal[(i & 0x0F) | 0x10]
            : pal[i];
        EXPECT_EQ(palBack[i], expected) << "palette " << i;
    }
}

TEST(DebugAccess, DisassembleMatchesProgram)
{
    FullControlHarness h;
    const auto line = h.machine.disassemble(0xC000);
    EXPECT_NE(std::string::npos, line.find("LDA #$01")) << line;
    const auto line2 = h.machine.disassemble(0xC00A);
    EXPECT_NE(std::string::npos, line2.find("LDA $4016")) << line2;
    const auto line3 = h.machine.disassemble(0xC00F);
    EXPECT_NE(std::string::npos, line3.find("JMP $C000")) << line3;
    // Side-effect-free: time does not move.
    const auto before = h.machine.masterCycle();
    (void)h.machine.disassemble(0xC000);
    EXPECT_EQ(h.machine.masterCycle(), before);
}

namespace {

class HashSink final : public cutenes::IVideoSink {
public:
    void onFrame(const cutenes::VideoFrame& frame) override
    {
        const auto* bytes =
            reinterpret_cast<const std::uint8_t*>(frame.pixels);
        std::uint32_t hash = 0x811C9DC5u;
        for (std::size_t i = 0; i < 256 * 240 * 4; ++i) {
            hash ^= bytes[i];
            hash *= 0x01000193u;
        }
        hashes.push_back(hash);
    }
    std::vector<std::uint32_t> hashes;
};

} // namespace

TEST(ScriptedInput, IdenticalToPerFrameCalls)
{
    const auto rom = makeEchoRom();
    std::vector<cutenes::ButtonSet> script(20);
    for (int i = 0; i < 20; ++i) {
        script[i][static_cast<std::size_t>(cutenes::Button::A)] =
            (i % 3) == 0;
        script[i][static_cast<std::size_t>(cutenes::Button::Right)] =
            (i % 5) == 0;
        script[i][static_cast<std::size_t>(cutenes::Button::Start)] =
            (i % 7) == 0;
    }

    HashSink sinkA;
    HashSink sinkB;
    for (int pass = 0; pass < 2; ++pass) {
        cutenes::Machine machine;
        ASSERT_EQ(machine.loadCartridge(rom.data(), rom.size()).error,
            cutenes::LoadError::Ok);
        HashSink& sink = pass == 0 ? sinkA : sinkB;
        machine.setVideoSink(&sink);
        machine.powerOn();
        if (pass == 0) {
            const cutenes::InputScript is{script.data(), script.size()};
            machine.runFrames(50, &is); // 20 scripted + 30 holding last
        } else {
            for (std::uint32_t f = 0; f < 50; ++f) {
                const std::size_t idx =
                    f < script.size() ? f : script.size() - 1;
                machine.setController(cutenes::Port::One, script[idx]);
                machine.runFrames(1);
            }
        }
        // Controller state observable after the run: last entry held.
        EXPECT_EQ(machine.controller(cutenes::Port::One).to_ulong(),
            script.back().to_ulong());
    }
    ASSERT_EQ(sinkA.hashes.size(), sinkB.hashes.size());
    for (std::size_t i = 0; i < sinkA.hashes.size(); ++i) {
        EXPECT_EQ(sinkA.hashes[i], sinkB.hashes[i]) << "frame " << i;
    }
}

TEST(ScriptedInput, AffectsGameExecution)
{
    const auto rom = makeEchoRom();
    std::uint8_t withA = 0;
    std::uint8_t withoutA = 0;
    for (int pass = 0; pass < 2; ++pass) {
        cutenes::Machine machine;
        ASSERT_EQ(machine.loadCartridge(rom.data(), rom.size()).error,
            cutenes::LoadError::Ok);
        machine.powerOn();
        cutenes::ButtonSet held;
        if (pass == 0) {
            held[static_cast<std::size_t>(cutenes::Button::A)] = 1;
        }
        std::vector<cutenes::ButtonSet> script(30, held);
        const cutenes::InputScript is{script.data(), script.size()};
        machine.runFrames(30, &is);
        std::uint8_t& out = pass == 0 ? withA : withoutA;
        machine.readMemory(0x10, 1, &out);
    }
    // The echo program stores button A's state (bit 0 of the read) in $10.
    EXPECT_EQ(withA & 1, 1);
    EXPECT_EQ(withoutA & 1, 0);
}

// --- Region selection ----------------------------------------------------------
// A NES 2.0 header's TV-system field (byte 12, bits 0-1) selects PAL or NTSC
// timing. PAL runs 50 frames a second, so a frame is longer and produces more
// audio samples than an NTSC frame.

namespace {

struct CountingAudio final : cutenes::IAudioSink {
    void onSamples(const std::int16_t*, std::size_t count, std::uint64_t) override
    {
        samples += count;
    }
    std::size_t samples = 0;
};

std::vector<std::uint8_t> makeNes2Rom(std::uint8_t tvSystem)
{
    std::vector<std::uint8_t> rom = makeEchoRom();
    rom[7] = 0x08;      // NES 2.0
    rom[12] = tvSystem; // 0 NTSC, 1 PAL
    return rom;
}

} // namespace

TEST(Region, Nes2HeaderSelectsTheTvSystem)
{
    for (const auto& [tv, expected] :
        {std::pair{std::uint8_t{0}, cutenes::Region::Ntsc},
            std::pair{std::uint8_t{1}, cutenes::Region::Pal}}) {
        auto rom = makeNes2Rom(tv);
        cutenes::Machine machine;
        ASSERT_EQ(machine.loadCartridge(rom.data(), rom.size()).error,
            cutenes::LoadError::Ok);
        EXPECT_EQ(machine.region(), expected) << "tv system " << int(tv);
    }
}

TEST(Region, PalFramesCarryMoreAudioThanNtscFrames)
{
    std::size_t samples[2] = {0, 0};
    for (int pal = 0; pal < 2; ++pal) {
        auto rom = makeNes2Rom(static_cast<std::uint8_t>(pal));
        CountingAudio audio;
        cutenes::Machine machine;
        machine.setAudioSink(&audio); // set before load: must survive it
        ASSERT_EQ(machine.loadCartridge(rom.data(), rom.size()).error,
            cutenes::LoadError::Ok);
        machine.powerOn();
        machine.runFrames(120);
        samples[pal] = audio.samples;
    }
    EXPECT_GT(samples[0], 0u) << "NTSC sink received audio";
    EXPECT_GT(samples[1], 0u) << "PAL sink must survive the region rebuild";
    // 120 frames at 48 kHz: NTSC 60.0988 fps = 1.9967 s = ~95.8k samples,
    // PAL 50.0070 fps = 2.3997 s = ~115.2k samples (within 1%).
    EXPECT_NEAR(static_cast<double>(samples[0]), 95840.0, 960.0);
    EXPECT_NEAR(static_cast<double>(samples[1]), 115186.0, 1152.0);
}

// Memory dump (issue #68; specs/001-emulation-core/memdump-design.md and
// contracts/core-api.md "Memory dump"). memoryDump() returns the bytes
// dumpMemory() writes, so API clients can get a dump without file I/O.
// Every section is checked against the side-effect-free accessor that
// defines it; integers are little-endian.
namespace {

std::uint64_t le64(const std::vector<std::uint8_t>& b, std::size_t at)
{
    std::uint64_t v = 0;
    for (int i = 7; i >= 0; --i) {
        v = (v << 8) | b.at(at + static_cast<std::size_t>(i));
    }
    return v;
}

std::uint32_t le32(const std::vector<std::uint8_t>& b, std::size_t at)
{
    std::uint32_t v = 0;
    for (int i = 3; i >= 0; --i) {
        v = (v << 8) | b.at(at + static_cast<std::size_t>(i));
    }
    return v;
}

} // namespace

TEST(MemoryDump, SectionsMatchTheDebugAccessors)
{
    FullControlHarness h;
    h.machine.runFrames(3);
    // Distinctive content in every PPU region the dump covers.
    const std::uint8_t nt[4] = {0xDE, 0xAD, 0xBE, 0xEF};
    h.machine.writeVram(0x2123, nt, sizeof nt);
    std::uint8_t oam[256];
    for (int i = 0; i < 256; ++i) {
        oam[i] = static_cast<std::uint8_t>(255 - i);
    }
    h.machine.writeOam(oam);
    const std::uint8_t ram[2] = {0x5A, 0xA5};
    ASSERT_EQ(h.machine.writeMemory(0x0300, ram, sizeof ram),
        cutenes::WriteError::Ok);

    const std::vector<std::uint8_t> d = h.machine.memoryDump(0x1122334455667788ull);
    ASSERT_GE(d.size(), 8u + 24u + 0x10000u + 8u + 256u + 32u + 4u);
    EXPECT_EQ(std::string(d.begin(), d.begin() + 8), "CUNEDMP1");
    EXPECT_EQ(le64(d, 8), 0x1122334455667788ull) << "timestamp is the caller's";
    EXPECT_EQ(le64(d, 16), h.machine.frameCounter());
    EXPECT_EQ(le64(d, 24), h.machine.masterCycle());

    std::size_t at = 32;
    for (std::uint32_t a = 0; a < 0x10000; ++a) {
        ASSERT_EQ(d[at + a], h.machine.peek(static_cast<std::uint16_t>(a)))
            << "CPU byte " << a;
    }
    EXPECT_EQ(d[at + 0x0300], 0x5A);
    at += 0x10000;

    std::uint8_t regs[8];
    h.machine.readPpuRegisters(regs);
    EXPECT_EQ(0, std::memcmp(d.data() + at, regs, 8));
    at += 8;
    std::uint8_t oamBack[256];
    h.machine.readOam(oamBack);
    EXPECT_EQ(0, std::memcmp(d.data() + at, oamBack, 256));
    at += 256;
    std::uint8_t pal[32];
    h.machine.readPalette(pal);
    EXPECT_EQ(0, std::memcmp(d.data() + at, pal, 32));
    at += 32;

    const std::uint32_t prgRam = le32(d, at);
    EXPECT_EQ(prgRam, h.machine.cartridgeInfo().prgRamSize);
    at += 4;
    for (std::uint32_t i = 0; i < prgRam && i < 0x2000; ++i) {
        ASSERT_EQ(d[at + i], h.machine.peek(static_cast<std::uint16_t>(0x6000 + i)))
            << "PRG-RAM byte " << i;
    }
    at += prgRam;

    // The whole PPU bus: pattern tables, nametables, palette.
    ASSERT_EQ(std::string(d.begin() + static_cast<std::ptrdiff_t>(at),
                  d.begin() + static_cast<std::ptrdiff_t>(at + 4)),
        "VRAM");
    at += 4;
    ASSERT_EQ(le32(d, at), 0x4000u);
    at += 4;
    std::vector<std::uint8_t> vram(0x4000);
    h.machine.readVram(0x0000, vram.size(), vram.data());
    EXPECT_EQ(0, std::memcmp(d.data() + at, vram.data(), vram.size()));
    EXPECT_EQ(d[at + 0x2123], 0xDE) << "nametable bytes are dumped, not zeros";
    at += 0x4000;
    EXPECT_EQ(at, d.size()) << "no trailing bytes";
}

TEST(MemoryDump, IsDeterministicAndHasNoSideEffects)
{
    FullControlHarness h;
    h.machine.runFrames(2);
    const auto before = h.machine.cpu();
    const auto a = h.machine.memoryDump(7);
    const auto b = h.machine.memoryDump(7);
    EXPECT_EQ(a, b);
    const auto after = h.machine.cpu();
    EXPECT_EQ(before.pc, after.pc);
    EXPECT_EQ(before.masterCycle, after.masterCycle);
    std::uint8_t regs[8];
    h.machine.readPpuRegisters(regs);
    EXPECT_EQ(h.machine.memoryDump(7), a) << "reading registers changed nothing";
}

// dumpMemory keeps working (maintainer, 2026-10-29: it helps debugging):
// it writes <datetime>.dmp whose bytes are memoryDump() of the same instant.
TEST(MemoryDump, DumpMemoryWritesTheSameBytesToAFile)
{
    namespace fs = std::filesystem;
    FullControlHarness h;
    h.machine.runFrames(2);
    const fs::path dir = fs::temp_directory_path()
        / ("cutenes-dump-test-" + std::to_string(h.machine.masterCycle()));
    fs::remove_all(dir);
    const std::string written = h.machine.dumpMemory(dir.string());
    ASSERT_TRUE(fs::exists(written)) << written;
    EXPECT_EQ(fs::path(written).parent_path(), dir);
    EXPECT_EQ(fs::path(written).extension(), ".dmp");

    std::ifstream in(written, std::ios::binary);
    const std::vector<std::uint8_t> file(
        (std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    ASSERT_GE(file.size(), 16u);
    EXPECT_EQ(file, h.machine.memoryDump(le64(file, 8)));
    in.close();
    fs::remove_all(dir);
}

// setInstructionTrace (core-api.md "Instruction trace"): a callback is
// called once per instruction; an empty callback turns tracing off.
TEST(InstructionTrace, EmptyCallbackTurnsTracingOff)
{
    FullControlHarness h;
    int calls = 0;
    h.machine.setInstructionTrace([&](const cutenes::TracePoint&) { ++calls; });
    h.machine.stepInstruction();
    EXPECT_EQ(calls, 1);
    h.machine.setInstructionTrace(nullptr);
    h.machine.stepInstruction(); // must not call an empty function
    h.machine.runFrames(1);
    EXPECT_EQ(calls, 1);
}

// A region change rebuilds the CPU and PPU; the installed trace must follow
// them like the media sinks do, and report the new PPU's position.
TEST(InstructionTrace, SurvivesARegionRebuild)
{
    cutenes::Machine machine;
    std::vector<cutenes::TracePoint> points;
    machine.setInstructionTrace([&](const cutenes::TracePoint& tp) { points.push_back(tp); });
    auto rom = makeNes2Rom(1); // PAL: rebuilds the components
    ASSERT_EQ(machine.loadCartridge(rom.data(), rom.size()).error, cutenes::LoadError::Ok);
    ASSERT_EQ(machine.region(), cutenes::Region::Pal);
    machine.powerOn();
    machine.stepInstruction();
    machine.stepInstruction();
    ASSERT_EQ(points.size(), 2u);
    // $C000 LDA #$01 (2 cycles): PAL runs 3.2 dots per CPU cycle, so the
    // second boundary is 6 or 7 dots after the first, on the same line.
    EXPECT_EQ(points[0].cpu.pc, 0xC000);
    EXPECT_EQ(points[1].cpu.pc, 0xC002);
    EXPECT_EQ(points[1].scanline, points[0].scanline);
    const auto dots = points[1].dot - points[0].dot;
    EXPECT_TRUE(dots == 6 || dots == 7) << dots;
}

// sram() and loadSram() cover battery-backed RAM only (core contract,
// "Save states and SRAM"; #71). iNES flags 6 bit 1 is the battery bit. An
// NROM image without it still has work RAM at $6000 (research.md D10), and
// that RAM is not a save.
TEST(Sram, CartridgeWithoutBatteryHasNoSram)
{
    auto rom = makeEchoRom();
    ASSERT_EQ(rom[6] & 0x02, 0);
    cutenes::Machine machine;
    ASSERT_EQ(machine.loadCartridge(rom.data(), rom.size()).error, cutenes::LoadError::Ok);
    machine.powerOn();
    ASSERT_FALSE(machine.cartridgeInfo().battery);

    const cutenes::SramRange sram = machine.sram();
    EXPECT_EQ(sram.size, 0u);
    EXPECT_EQ(sram.data, nullptr);

    // loadSram is ignored: the work RAM keeps what it had.
    const std::uint8_t before = machine.peek(0x6000);
    const std::vector<std::uint8_t> save(8192, static_cast<std::uint8_t>(before ^ 0xFF));
    machine.loadSram(save.data(), save.size());
    EXPECT_EQ(machine.peek(0x6000), before);
}

TEST(Sram, BatteryCartridgeExposesItsRam)
{
    auto rom = makeEchoRom();
    rom[6] |= 0x02; // battery
    cutenes::Machine machine;
    ASSERT_EQ(machine.loadCartridge(rom.data(), rom.size()).error, cutenes::LoadError::Ok);
    machine.powerOn();
    ASSERT_TRUE(machine.cartridgeInfo().battery);

    const std::vector<std::uint8_t> save{0x12, 0x34, 0x56}; // shorter: padded
    machine.loadSram(save.data(), save.size());
    const cutenes::SramRange sram = machine.sram();
    ASSERT_EQ(sram.size, 8192u); // flags 8 = 0: the 8 KiB default
    EXPECT_EQ(sram.data[0], 0x12);
    EXPECT_EQ(sram.data[2], 0x56);
    EXPECT_EQ(machine.peek(0x6001), 0x34); // the CPU sees the loaded save
}
