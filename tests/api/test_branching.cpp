// Q4 branching (tasks A015, US3): save a slot, run recording hashes, load
// the slot and rerun; the hashes repeat. Expectations compare two runs in
// one test (spec Assumptions: no pinned frame hashes before 1.0).

#include "support.hpp"

#include <cstring>

using namespace cutenes::api;
using namespace cutenes::api::test;

TEST(ApiBranching, TenSlotCyclesReplayIdentically)
{
    Session s(testConfig());
    ok(s, "core.load_rom", {{"path", "instr_test-v5/rom_singles/01-basics.nes"}});
    ok(s, "core.run_frames", {{"frames", 20}});
    ok(s, "session.save_slot", {{"name", "branch"}});

    const auto record = [&] {
        std::vector<std::string> hashes;
        for (int i = 0; i < 10; ++i) {
            hashes.push_back(ok(s, "core.run_frames", {{"frames", 50}})["frameHash"]);
        }
        return hashes; // 500 frames
    };
    const auto first = record();
    for (int cycle = 0; cycle < 10; ++cycle) {
        ok(s, "session.load_slot", {{"name", "branch"}});
        EXPECT_EQ(ok(s, "core.counters")["frame"], 20);
        EXPECT_EQ(record(), first) << "cycle " << cycle;
    }
}

TEST(ApiBranching, SlotNamesAreValidatedAndOverwrite)
{
    Session s(testConfig());
    ok(s, "core.load_rom", {{"path", "instr_test-v5/rom_singles/01-basics.nes"}});
    fails(s, "session.save_slot", {{"name", "bad name"}}, "InvalidArguments");
    fails(s, "session.save_slot", {{"name", std::string(65, 'a')}}, "InvalidArguments");
    fails(s, "session.save_slot", {{"name", ""}}, "InvalidArguments");
    ok(s, "session.save_slot", {{"name", "Slot_1-a"}});
    ok(s, "core.run_frames", {{"frames", 5}});
    ok(s, "session.save_slot", {{"name", "Slot_1-a"}}); // overwrite
    const Json slots = ok(s, "session.list_slots")["slots"];
    ASSERT_EQ(slots.size(), 1u);
    EXPECT_EQ(slots[0]["frame"], 5);
    ok(s, "session.delete_slot", {{"name", "Slot_1-a"}});
    fails(s, "session.load_slot", {{"name", "Slot_1-a"}}, "UnknownHandle");
    fails(s, "session.delete_slot", {{"name", "Slot_1-a"}}, "UnknownHandle");
}

TEST(ApiBranching, ASlotFromAnotherCartridgeIsWrongCartridge)
{
    Session s(testConfig());
    ok(s, "core.load_rom", {{"path", "instr_test-v5/rom_singles/01-basics.nes"}});
    ok(s, "session.save_slot", {{"name", "a"}});
    // A cartridge on a different board: the header's cartridgeId alone
    // refuses it (save-state-format.md). Same board, other game: below.
    ok(s, "core.load_rom", {{"blobId", upload(s, tinyRom())}});
    const auto before = stateBytes(s);
    EXPECT_EQ(fails(s, "session.load_slot", {{"name", "a"}}, "LoadStateError")["error"],
        "WrongCartridge");
    EXPECT_EQ(stateBytes(s), before);
}

// #92: two games on the same board (mapper, PRG/CHR sizes and mirroring)
// are still different games. blargg's 01-basics and 02-implied share their
// board, and a state from one must not load into the other: running the
// other game's code from the first game's CPU and RAM is a corrupted run,
// not a branch. The state names the ROM's contents (save-state-format.md,
// "ROM " section) and the load is refused with nothing changed.
TEST(ApiBranching, AStateFromAnotherGameOnTheSameBoardIsWrongCartridge)
{
    Session s(testConfig());
    ok(s, "core.load_rom", {{"path", "instr_test-v5/rom_singles/02-implied.nes"}});
    ok(s, "core.run_frames", {{"frames", 10}});
    const Json other = ok(s, "core.capture_state");

    ok(s, "core.load_rom", {{"path", "instr_test-v5/rom_singles/01-basics.nes"}});
    ok(s, "core.run_frames", {{"frames", 5}});
    const auto before = stateBytes(s);
    EXPECT_EQ(fails(s, "core.load_state", {{"blobId", other["blobId"]}}, "LoadStateError")["error"],
        "WrongCartridge");
    EXPECT_EQ(stateBytes(s), before);
}

namespace {

// Standard CRC-32 (IEEE 802.3, reflected), the header's payloadChecksum.
std::uint32_t crc32(const std::uint8_t* data, std::size_t n)
{
    std::uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < n; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

// The blob without one section, as a core that never wrote it would have
// made it: the sections after the 24-byte header are tag, length, payload,
// and the checksum at offset 20 covers them.
std::vector<std::uint8_t> withoutSection(const std::vector<std::uint8_t>& blob, const char* tag)
{
    std::vector<std::uint8_t> out(blob.begin(), blob.begin() + 24);
    for (std::size_t at = 24; at + 8 <= blob.size();) {
        const std::uint32_t length = blob[at + 4] | blob[at + 5] << 8 | blob[at + 6] << 16
            | static_cast<std::uint32_t>(blob[at + 7]) << 24;
        const std::size_t end = at + 8 + length;
        if (std::memcmp(&blob[at], tag, 4) != 0) out.insert(out.end(), blob.begin() + static_cast<std::ptrdiff_t>(at), blob.begin() + static_cast<std::ptrdiff_t>(end));
        at = end;
    }
    const std::uint32_t sum = crc32(out.data() + 24, out.size() - 24);
    for (int i = 0; i < 4; ++i) out[20 + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(sum >> (8 * i));
    return out;
}

} // namespace

// States written before the "ROM " section existed have only the board
// check, and still load into the same game (a reader skips what is not
// there; the format version is unchanged).
TEST(ApiBranching, AStateWithoutTheRomSectionStillLoads)
{
    Session s(testConfig());
    ok(s, "core.load_rom", {{"path", "instr_test-v5/rom_singles/01-basics.nes"}});
    ok(s, "core.run_frames", {{"frames", 10}});
    const auto blob = stateBytes(s);
    const auto old = withoutSection(blob, "ROM ");
    ASSERT_LT(old.size(), blob.size()) << "the state has a ROM section";
    ok(s, "core.run_frames", {{"frames", 5}});
    ok(s, "core.load_state", {{"blobId", upload(s, old)}});
    EXPECT_EQ(stateBytes(s), blob) << "the same state, which writes the section again";
}
