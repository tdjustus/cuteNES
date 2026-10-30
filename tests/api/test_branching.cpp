// Q4 branching (tasks A015, US3): save a slot, run recording hashes, load
// the slot and rerun; the hashes repeat. Expectations compare two runs in
// one test (spec Assumptions: no pinned frame hashes before 1.0).

#include "support.hpp"

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
    // The WrongCartridge discriminator is mapper, PRG/CHR sizes and
    // mirroring (save-state-format.md), so use a cartridge that differs.
    ok(s, "core.load_rom", {{"blobId", upload(s, tinyRom())}});
    const auto before = stateBytes(s);
    EXPECT_EQ(fails(s, "session.load_slot", {{"name", "a"}}, "LoadStateError")["error"],
        "WrongCartridge");
    EXPECT_EQ(stateBytes(s), before);
}
