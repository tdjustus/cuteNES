// core.* commands (tasks A014): one test per command-catalog.md row, on
// vendored test ROMs and on a synthetic NROM image.

#include "blargg.hpp"

#include <cutenes/version.hpp>

using namespace cutenes::api;
using namespace cutenes::api::test;

namespace {

const char* const kBasics = "instr_test-v5/rom_singles/01-basics.nes";

void loadTiny(Session& s, bool battery = false)
{
    ok(s, "core.load_rom", {{"blobId", upload(s, tinyRom(battery))}});
}

} // namespace

// Q3: the play loop. The ROM's own verdict is the expectation: blargg
// instr_test-v5 reports status 0 and "All 16 tests passed" (readme.txt).
TEST(ApiCore, PlayLoopReachesTheRomsDocumentedVerdict)
{
    Session s(testConfig());
    const Json loaded = ok(s, "core.load_rom", {{"path", "instr_test-v5/official_only.nes"}});
    EXPECT_EQ(loaded["cartridge"]["mapper"], 1);
    const auto verdict = runBlargg(s);
    ASSERT_TRUE(verdict.signature);
    EXPECT_EQ(verdict.status, 0) << verdict.text;
    EXPECT_NE(verdict.text.find("All 16 tests passed"), std::string::npos) << verdict.text;

    const Json screen = ok(s, "core.screen");
    EXPECT_TRUE(screen["hasFrame"]);
    const auto png = bytesOf(screen["png"]);
    ASSERT_GE(png.size(), 8u);
    EXPECT_EQ(png[1], 'P');
    EXPECT_EQ(screen["frameHash"], ok(s, "core.frame_hash")["frameHash"]);
}

TEST(ApiCore, LoadRomReportsTheHeader)
{
    Session s(testConfig());
    const Json c = ok(s, "core.load_rom", {{"path", kBasics}})["cartridge"];
    // 01-basics.nes header: 2 x 16 KiB PRG, 1 x 8 KiB CHR, mapper 0, vertical.
    EXPECT_EQ(c["format"], "iNES");
    EXPECT_EQ(c["mapper"], 0);
    EXPECT_EQ(c["prgSize"], 32768);
    EXPECT_EQ(c["chrSize"], 8192);
    EXPECT_EQ(c["mirroring"], "Vertical");
    EXPECT_EQ(ok(s, "core.cartridge_info")["cartridge"], c);
    EXPECT_EQ(ok(s, "core.region")["region"], "NTSC"); // iNES 1.0: the NTSC default
    EXPECT_EQ(ok(s, "core.run_state")["runState"], "Paused");
}

TEST(ApiCore, EjectReturnsToNeedsRom)
{
    Session s(testConfig());
    loadTiny(s);
    ok(s, "core.eject");
    EXPECT_EQ(ok(s, "core.run_state")["runState"], "NeedsRom");
    fails(s, "core.run_frames", {{"frames", 1}}, "NoCartridge");
}

TEST(ApiCore, PowerOnMatchesTheDocumentedColdBoot)
{
    Session s(testConfig());
    loadTiny(s);
    ok(s, "core.power_on");
    // research D4 / nestest.log line 1: P=$24, S=$FD after the 7-cycle reset,
    // PC from the reset vector ($C000 in the synthetic image).
    const Json cpu = ok(s, "core.get_cpu")["cpu"];
    EXPECT_EQ(cpu["p"], 0x24);
    EXPECT_EQ(cpu["s"], 0xFD);
    EXPECT_EQ(cpu["pc"], 0xC000);
    EXPECT_EQ(ok(s, "core.counters")["frame"], 0);
}

TEST(ApiCore, ResetJumpsThroughTheVectorAndDropsS)
{
    Session s(testConfig());
    loadTiny(s);
    ok(s, "core.set_cpu", {{"pc", 0x1234}, {"s", 0xFD}});
    ok(s, "core.reset");
    const Json cpu = ok(s, "core.get_cpu")["cpu"];
    // A 6502 reset runs three suppressed pushes: S decreases by 3.
    EXPECT_EQ(cpu["pc"], 0xC000);
    EXPECT_EQ(cpu["s"], 0xFA);
}

TEST(ApiCore, RunFramesAndFrameAdvanceCountFrames)
{
    Session s(testConfig());
    loadTiny(s);
    const Json r = ok(s, "core.run_frames", {{"frames", 5}});
    EXPECT_EQ(r["framesRun"], 5);
    EXPECT_EQ(r["frame"], 5);
    const Json a = ok(s, "core.frame_advance");
    EXPECT_EQ(a["frame"], 6);
    EXPECT_EQ(a["frameHash"], ok(s, "core.frame_hash")["frameHash"]);
    EXPECT_GT(a["masterCycle"].get<std::uint64_t>(), r["masterCycle"].get<std::uint64_t>());
}

TEST(ApiCore, RunFramesHonoursTheConfiguredCap)
{
    SessionConfig c = testConfig();
    c.maxRunFrames = 10;
    Session s(c);
    loadTiny(s);
    EXPECT_EQ(fails(s, "core.run_frames", {{"frames", 11}}, "LimitExceeded")["max"], 10);
    fails(s, "core.run_frames", {{"frames", 216001}}, "InvalidArguments");
}

TEST(ApiCore, RunFramesScriptDrivesControllerOne)
{
    Session s(testConfig());
    loadTiny(s);
    ok(s, "core.run_frames", {{"frames", 3}, {"script", {{"A"}, {"Start", "Up"}}}});
    // The last entry is held past the end of the script (FR-019).
    const Json held = ok(s, "core.get_controller", {{"port", 1}})["buttons"];
    EXPECT_EQ(held, (Json{"Up", "Start"}));
}

TEST(ApiCore, ControllersRoundTrip)
{
    Session s(testConfig());
    ok(s, "core.set_controller", {{"port", 2}, {"buttons", {"A", "B", "Left"}}});
    EXPECT_EQ(ok(s, "core.get_controller", {{"port", 2}})["buttons"], (Json{"Left", "B", "A"}));
    EXPECT_EQ(ok(s, "core.get_controller", {{"port", 1}})["buttons"], Json::array());
}

TEST(ApiCore, StepInstructionExecutesTheLoop)
{
    Session s(testConfig());
    loadTiny(s);
    const Json cpu = ok(s, "core.step_instruction", {{"count", 3}})["cpu"];
    EXPECT_EQ(cpu["pc"], 0xC000); // JMP $C000
    const Json before = ok(s, "core.counters");
    ok(s, "core.step_instruction");
    // JMP absolute takes 3 CPU cycles = 36 NTSC master cycles.
    EXPECT_EQ(ok(s, "core.counters")["masterCycle"].get<std::uint64_t>()
            - before["masterCycle"].get<std::uint64_t>(),
        36u);
}

TEST(ApiCore, PauseResumeAndAutomationPause)
{
    Session s(testConfig());
    loadTiny(s);
    EXPECT_EQ(ok(s, "core.resume")["runState"], "Running");
    ok(s, "core.frame_advance");
    const Json st = ok(s, "core.run_state");
    EXPECT_EQ(st["runState"], "Paused");
    EXPECT_TRUE(st["pausedByAutomation"]);
    EXPECT_TRUE(ok(s, "session.status")["pausedByAutomation"]);
    EXPECT_FALSE(ok(s, "core.resume")["pausedByAutomation"]);
    const Json p = ok(s, "core.pause");
    EXPECT_EQ(p["runState"], "Paused");
    EXPECT_FALSE(p["pausedByAutomation"]);
}

TEST(ApiCore, Mmc3RevisionRoundTrips)
{
    Session s(testConfig());
    EXPECT_EQ(ok(s, "core.get_mmc3_revision")["revision"], "auto");
    ok(s, "core.set_mmc3_revision", {{"revision", "A"}});
    EXPECT_EQ(ok(s, "core.get_mmc3_revision")["revision"], "A");
}

TEST(ApiCore, CpuRegistersWriteAndForceBit5)
{
    Session s(testConfig());
    loadTiny(s);
    const Json cpu = ok(s, "core.set_cpu", {{"a", 0x12}, {"x", 0x34}, {"y", 0x56}, {"p", 0x00}})["cpu"];
    EXPECT_EQ(cpu["a"], 0x12);
    EXPECT_EQ(cpu["x"], 0x34);
    EXPECT_EQ(cpu["y"], 0x56);
    EXPECT_EQ(cpu["p"], 0x20); // bit 5 always reads 1 (research D4)
    EXPECT_EQ(cpu["pc"], 0xC000); // untouched
}

TEST(ApiCore, DisassembleWalksInstructions)
{
    Session s(testConfig());
    loadTiny(s);
    const Json lines = ok(s, "core.disassemble", {{"addr", 0xC000}, {"count", 2}})["lines"];
    ASSERT_EQ(lines.size(), 2u);
    EXPECT_EQ(lines[0]["addr"], 0xC000);
    EXPECT_NE(lines[0]["text"].get<std::string>().find("JMP $C000"), std::string::npos);
    EXPECT_EQ(lines[1]["addr"], 0xC003); // 3-byte instruction
}

TEST(ApiCore, TraceRecordsEachInstruction)
{
    Session s(testConfig());
    loadTiny(s);
    const Json lines = ok(s, "core.trace", {{"instructions", 4}})["lines"];
    ASSERT_EQ(lines.size(), 4u);
    for (const auto& l : lines) {
        EXPECT_EQ(l["cpu"]["pc"], 0xC000);
        EXPECT_NE(l["text"].get<std::string>().find("JMP"), std::string::npos);
    }
    // 3 CPU cycles = 9 PPU dots between consecutive JMPs.
    const auto dots = [](const Json& l) {
        return l["scanline"].get<int>() * 341 + l["dot"].get<int>();
    };
    EXPECT_EQ(dots(lines[1]) - dots(lines[0]), 9);
}

TEST(ApiCore, PpuDebugAccessRoundTrips)
{
    Session s(testConfig());
    loadTiny(s);
    EXPECT_EQ(bytesOf(ok(s, "core.read_ppu_registers")["bytes"]).size(), 8u);

    // Nametable RAM ($2000-$2FFF) round-trips. Horizontal mirroring
    // (flags 6 bit 0 clear) maps $2400 onto $2000.
    ok(s, "core.write_vram", {{"addr", 0x2000}, {"bytes", b64({1, 2, 3})}});
    EXPECT_EQ(bytesOf(ok(s, "core.read_vram", {{"addr", 0x2400}, {"length", 3}})["bytes"]),
        (std::vector<std::uint8_t>{1, 2, 3}));

    std::vector<std::uint8_t> oam(256);
    for (std::size_t i = 0; i < 256; ++i) oam[i] = static_cast<std::uint8_t>(i);
    ok(s, "core.write_oam", {{"bytes", b64(oam)}});
    EXPECT_EQ(bytesOf(ok(s, "core.read_oam")["bytes"]), oam);

    std::vector<std::uint8_t> pal(32);
    for (std::size_t i = 0; i < 32; ++i) pal[i] = static_cast<std::uint8_t>(i);
    ok(s, "core.write_palette", {{"bytes", b64(pal)}});
    const auto back = bytesOf(ok(s, "core.read_palette")["bytes"]);
    ASSERT_EQ(back.size(), 32u);
    // $3F10/$3F14/$3F18/$3F1C mirror $3F00/$3F04/$3F08/$3F0C (nesdev
    // "PPU palettes"): the later writes win.
    EXPECT_EQ(back[0], 0x10);
    EXPECT_EQ(back[0x10], 0x10);
    EXPECT_EQ(back[1], 1);
    fails(s, "core.write_oam", {{"bytes", b64({1, 2})}}, "InvalidArguments");
}

TEST(ApiCore, SaveStateBlobRoundTrip)
{
    Session s(testConfig());
    ok(s, "core.load_rom", {{"path", kBasics}});
    ok(s, "core.run_frames", {{"frames", 30}});
    const Json cap = ok(s, "core.capture_state");
    const auto state = download(s, cap["blobId"]);
    EXPECT_EQ(state.size(), cap["size"].get<std::size_t>());
    const std::string hash = ok(s, "core.run_frames", {{"frames", 30}})["frameHash"];
    ok(s, "core.load_state", {{"blobId", cap["blobId"]}});
    EXPECT_EQ(ok(s, "core.counters")["frame"], 30);
    EXPECT_EQ(ok(s, "core.run_frames", {{"frames", 30}})["frameHash"], hash);
    // The same state, uploaded again.
    ok(s, "core.load_state", {{"blobId", upload(s, state)}});
    EXPECT_EQ(ok(s, "core.counters")["frame"], 30);
}

TEST(ApiCore, SramNeedsABatteryAndRoundTrips)
{
    Session s(testConfig());
    loadTiny(s, false);
    fails(s, "core.read_sram", {{"offset", 0}, {"length", 1}}, "NoBattery");
    loadTiny(s, true);
    ok(s, "core.load_sram", {{"bytes", b64({0xAA, 0xBB})}});
    const Json r = ok(s, "core.read_sram", {{"offset", 0}, {"length", 2}});
    EXPECT_EQ(bytesOf(r["bytes"]), (std::vector<std::uint8_t>{0xAA, 0xBB}));
    EXPECT_EQ(r["size"], 8192); // flags 8 = 0 means 8 KiB (research D10)
    // SRAM is the CPU's $6000-$7FFF.
    EXPECT_EQ(peekByte(s, 0x6001), 0xBB);
}

TEST(ApiCore, ScreenRawIsTheFrameBytes)
{
    Session s(testConfig());
    loadTiny(s);
    EXPECT_FALSE(ok(s, "core.frame_hash")["hasFrame"]);
    ok(s, "core.run_frames", {{"frames", 2}});
    const Json raw = ok(s, "core.screen", {{"format", "raw"}});
    const auto bytes = download(s, raw["blobId"]);
    ASSERT_EQ(bytes.size(), 256u * 240u * 4u);
    EXPECT_EQ(media::hashHex(media::fnv1a32(bytes.data(), bytes.size())), raw["frameHash"].get<std::string>());
    EXPECT_EQ(raw["frameIndex"].get<std::uint64_t>() + 1, raw["frame"].get<std::uint64_t>());
}

TEST(ApiCore, AudioArrivesInMasterCycleOrder)
{
    Session s(testConfig());
    loadTiny(s);
    ok(s, "core.run_frames", {{"frames", 10}});
    const Json a = ok(s, "core.audio", {{"maxSamples", 4096}});
    EXPECT_EQ(a["sampleRate"], 48000);
    EXPECT_EQ(bytesOf(a["pcm"]).size(), a["samples"].get<std::size_t>() * 2);
    EXPECT_GT(a["samples"].get<std::size_t>(), 0u);
    const Json b = ok(s, "core.audio", {{"sinceMasterCycle", a["next"]}, {"maxSamples", 4096}});
    if (b["samples"].get<std::size_t>() > 0) {
        EXPECT_GE(b["startMasterCycle"].get<std::uint64_t>(), a["next"].get<std::uint64_t>());
    }
    // 10 NTSC frames at 48 kHz is about 8000 samples in total.
    std::size_t total = 0;
    std::uint64_t since = 0;
    for (;;) {
        const Json c = ok(s, "core.audio", {{"sinceMasterCycle", since}});
        total += c["samples"].get<std::size_t>();
        since = c["next"];
        if (!c["truncated"]) break;
    }
    EXPECT_NEAR(static_cast<double>(total), 48000.0 * 10 / 60.0988, 200.0);
}

TEST(ApiCore, DumpMemoryIsTheCoreDumpFormat)
{
    Session s(testConfig());
    loadTiny(s);
    const Json d = ok(s, "core.dump_memory");
    const auto bytes = download(s, d["blobId"]);
    ASSERT_GE(bytes.size(), 8u);
    EXPECT_EQ(std::string(bytes.begin(), bytes.begin() + 8), "CUNEDMP1");
}

TEST(ApiCore, StatusAndVersion)
{
    Session s(testConfig());
    Json st = ok(s, "session.status");
    EXPECT_EQ(st["mode"], "headless");
    EXPECT_FALSE(st["cartridgeLoaded"]);
    EXPECT_EQ(st["coreVersion"], cutenes::coreVersionString());
    loadTiny(s);
    st = ok(s, "session.status");
    EXPECT_TRUE(st["cartridgeLoaded"]);
    EXPECT_EQ(st["cartridge"]["mapper"], 0);
    EXPECT_EQ(ok(s, "api.version")["core"], cutenes::coreVersionString());
}

TEST(ApiCore, LibraryListIsRelativeAndSorted)
{
    Session s(testConfig());
    const Json r = ok(s, "session.library_list");
    const auto& roms = r["roms"];
    ASSERT_FALSE(roms.empty());
    EXPECT_TRUE(std::is_sorted(roms.begin(), roms.end()));
    for (const auto& rom : roms) {
        EXPECT_NE(rom.get<std::string>().front(), '/');
    }
}

// The GUI's library can move while it runs (003 US2-4); session.library_list
// and core.load_rom paths then follow the new root (003 FR-074).
TEST(ApiCore, TheLibraryRootCanChange)
{
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "cutenes-api-library-root-test";
    fs::remove_all(root);
    fs::create_directories(root / "sub");
    fs::copy_file(fs::path(romDir()) / "instr_test-v5/rom_singles/01-basics.nes", root / "sub/game.nes");

    SessionConfig c = testConfig();
    c.libraryRoot.clear();
    Session s(c);
    EXPECT_TRUE(ok(s, "session.library_list")["roms"].empty());
    EXPECT_FALSE(ok(s, "session.status")["libraryConfigured"]);
    s.setLibraryRoot(root.string());
    EXPECT_EQ(ok(s, "session.library_list")["roms"], Json::array({"sub/game.nes"}));
    EXPECT_TRUE(ok(s, "session.status")["libraryConfigured"]);
    ok(s, "core.load_rom", {{"path", "sub/game.nes"}});
    fs::remove_all(root);
}

TEST(ApiCore, WriteDumpWritesIntoTheDebugDirectory)
{
    SessionConfig c = testConfig();
    c.debugDir = (std::filesystem::temp_directory_path() / "cutenes-api-dump-test").string();
    std::filesystem::remove_all(c.debugDir);
    Session s(c);
    loadTiny(s);
    const std::string file = ok(s, "session.write_dump")["file"];
    EXPECT_EQ(file.find('/'), std::string::npos);
    EXPECT_TRUE(std::filesystem::exists(std::filesystem::path(c.debugDir) / file));
    std::filesystem::remove_all(c.debugDir);
}
