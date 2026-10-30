// Q6 errors (tasks A017, FR-005): each named error, and each leaves the
// machine state byte-identical (a capture_state before and after).

#include "support.hpp"

#include <filesystem>
#include <fstream>

using namespace cutenes::api;
using namespace cutenes::api::test;

namespace {

struct Loaded {
    Session s{testConfig()};
    Loaded()
    {
        ok(s, "core.load_rom", {{"path", "instr_test-v5/rom_singles/01-basics.nes"}});
        ok(s, "core.run_frames", {{"frames", 10}});
    }
};

} // namespace

TEST(ApiErrors, EachErrorLeavesTheStateUnchanged)
{
    Loaded l;
    Session& s = l.s;
    const auto before = stateBytes(s);
    const auto expectUnchanged = [&](const char* what) {
        EXPECT_EQ(stateBytes(s), before) << what;
    };

    fails(s, "core.nope", Json::object(), "UnknownCommand");
    expectUnchanged("UnknownCommand");
    EXPECT_EQ(fails(s, "core.run_frames", {{"frames", 0}}, "InvalidArguments")["path"], "/frames");
    expectUnchanged("InvalidArguments");
    EXPECT_EQ(fails(s, "core.run_frames", {{"frames", 1}, {"turbo", true}}, "InvalidArguments")["path"],
        "/turbo");
    fails(s, "core.load_rom", {{"path", "../CMakeLists.txt"}}, "PathOutsideLibrary");
    fails(s, "core.load_rom", {{"path", "/etc/hosts"}}, "PathOutsideLibrary");
    expectUnchanged("PathOutsideLibrary");
    fails(s, "core.load_rom", {{"path", "does/not/exist.nes"}}, "NotFound");
    fails(s, "core.load_rom", Json::object(), "InvalidArguments");
    expectUnchanged("NotFound");

    // A rejected ROM: the core names the defect, and the loaded game stays.
    const Json bad = fails(s, "core.load_rom", {{"blobId", upload(s, {'N', 'O', 'P', 'E'})}}, "LoadError");
    EXPECT_EQ(bad["error"], "NotAnInesFile");
    auto truncated = tinyRom();
    truncated.resize(100);
    EXPECT_EQ(fails(s, "core.load_rom", {{"blobId", upload(s, truncated)}}, "LoadError")["error"],
        "TruncatedFile");
    expectUnchanged("LoadError");

    EXPECT_EQ(fails(s, "core.load_state", {{"bytes", b64({1, 2, 3, 4})}}, "LoadStateError")["error"],
        "BadMagic");
    expectUnchanged("LoadStateError");
    fails(s, "core.load_state", {{"blobId", "blob-999"}}, "UnknownHandle");
    fails(s, "session.blob_read", {{"blobId", "blob-999"}, {"offset", 0}, {"length", 1}}, "UnknownHandle");
    expectUnchanged("UnknownHandle");
    fails(s, "core.write_cpu_register", {{"addr", 0x0100}, {"value", 1}}, "WriteError");
    expectUnchanged("WriteError");

    SessionConfig capped = testConfig();
    capped.maxRunFrames = 1;
    Session c(capped);
    ok(c, "core.load_rom", {{"path", "instr_test-v5/rom_singles/01-basics.nes"}});
    const auto cappedBefore = stateBytes(c);
    fails(c, "core.run_frames", {{"frames", 2}}, "LimitExceeded");
    EXPECT_EQ(stateBytes(c), cappedBefore) << "LimitExceeded";
}

TEST(ApiErrors, NoCartridgeForNeedsRomCommands)
{
    Session s(testConfig());
    for (const auto& c : s.registry().catalog()) {
        if (c["needsRom"]) {
            // Arguments may be invalid; the schema check runs first, so give
            // the minimal valid ones where needed.
            Json args = Json::object();
            const std::string name = c["name"];
            if (name == "core.run_frames") args = {{"frames", 1}};
            if (name == "core.trace") args = {{"instructions", 1}};
            if (name == "core.read_sram") args = {{"offset", 0}, {"length", 1}};
            if (name == "core.load_sram") args = {{"bytes", ""}};
            if (name == "core.load_state") args = {{"bytes", ""}};
            if (name == "session.save_slot" || name == "session.load_slot") args = {{"name", "a"}};
            fails(s, name, args, "NoCartridge");
        }
    }
}

TEST(ApiErrors, SymlinkOutOfTheLibraryIsRefused)
{
    namespace fs = std::filesystem;
    const fs::path lib = fs::temp_directory_path() / "cutenes-api-lib-test";
    const fs::path outside = fs::temp_directory_path() / "cutenes-api-outside.nes";
    fs::remove_all(lib);
    fs::create_directories(lib / "sub");
    const auto rom = tinyRom();
    std::ofstream(outside, std::ios::binary)
        .write(reinterpret_cast<const char*>(rom.data()), static_cast<std::streamsize>(rom.size()));
    std::ofstream(lib / "sub" / "inside.nes", std::ios::binary)
        .write(reinterpret_cast<const char*>(rom.data()), static_cast<std::streamsize>(rom.size()));
    std::error_code ec;
    fs::create_symlink(outside, lib / "escape.nes", ec);

    SessionConfig c = testConfig();
    c.libraryRoot = lib.string();
    Session s(c);
    ok(s, "core.load_rom", {{"path", "sub/inside.nes"}});
    fails(s, "core.load_rom", {{"path", "sub/../../cutenes-api-outside.nes"}}, "PathOutsideLibrary");
    if (!ec) {
        fails(s, "core.load_rom", {{"path", "escape.nes"}}, "PathOutsideLibrary");
        EXPECT_EQ(ok(s, "session.library_list")["roms"], (Json{"sub/inside.nes"}));
    }
    fs::remove_all(lib);
    fs::remove(outside);
}

TEST(ApiErrors, UploadsMustBeCompleteAndInOrder)
{
    Session s(testConfig());
    const std::string id = ok(s, "session.blob_begin", {{"size", 4}})["blobId"];
    // Not loadable before commit (spec edge case).
    fails(s, "core.load_rom", {{"blobId", id}}, "UnknownHandle");
    EXPECT_EQ(fails(s, "session.blob_chunk", {{"blobId", id}, {"offset", 2}, {"bytes", b64({1})}},
                  "InvalidArguments")["path"],
        "/offset");
    ok(s, "session.blob_chunk", {{"blobId", id}, {"offset", 0}, {"bytes", b64({1, 2})}});
    fails(s, "session.blob_commit", {{"blobId", id}}, "InvalidArguments");
    EXPECT_EQ(fails(s, "session.blob_chunk", {{"blobId", id}, {"offset", 2}, {"bytes", b64({3, 4, 5})}},
                  "LimitExceeded")["max"],
        4);
    ok(s, "session.blob_chunk", {{"blobId", id}, {"offset", 2}, {"bytes", b64({3, 4})}});
    ok(s, "session.blob_commit", {{"blobId", id}});
    EXPECT_EQ(download(s, id), (std::vector<std::uint8_t>{1, 2, 3, 4}));
    fails(s, "session.blob_chunk", {{"blobId", id}, {"offset", 4}, {"bytes", b64({1})}}, "UnknownHandle");
    ok(s, "session.blob_delete", {{"blobId", id}});
    fails(s, "session.blob_delete", {{"blobId", id}}, "UnknownHandle");
}
