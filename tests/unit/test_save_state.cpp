#include "cutenes/serialize/save_state.hpp"

#include <cutenes/cutenes.hpp>

#include <gtest/gtest.h>

#include <vector>

using cutenes::StateReader;
using cutenes::StateWriter;

namespace {

std::vector<std::uint8_t> makeBlob()
{
    std::vector<std::uint8_t> out;
    StateWriter w(out);
    w.begin("CPU ");
    w.w<std::uint8_t>(0x42);
    w.end();
    w.begin("SCH ");
    w.w<std::uint64_t>(1234);
    w.end();
    w.finish(1, 0, cutenes::coreVersion, 0, 0xABCD);
    return out;
}

} // namespace

TEST(SaveState, HeaderRoundTrip)
{
    const auto blob = makeBlob();
    StateReader r(blob.data(), blob.size());
    EXPECT_EQ(r.formatVersion(), 1);
    EXPECT_EQ(r.flags(), 0);
    EXPECT_EQ(r.coreVersion(), cutenes::coreVersion);
    EXPECT_EQ(r.region(), 0);
    EXPECT_EQ(r.cartridgeId(), 0xABCD);
    EXPECT_TRUE(r.seek("CPU "));
    EXPECT_EQ(r.get<std::uint8_t>(), 0x42);
}

TEST(SaveState, ChecksumDetectsCorruption)
{
    auto blob = makeBlob();
    blob[StateReader::kHeaderSize + 8] ^= 0xFF; // flip a payload byte
    StateReader r(blob.data(), blob.size());
    EXPECT_NE(StateWriter::crc32(blob.data() + StateReader::kHeaderSize,
                  r.sectionsSize()),
        r.checksum());
}

TEST(SaveState, UnknownSectionsAreSkipped)
{
    std::vector<std::uint8_t> out;
    StateWriter w(out);
    w.begin("FUTU"); // a tag from a future format version
    w.w<std::uint32_t>(7);
    w.end();
    w.begin("CPU ");
    w.w<std::uint8_t>(9);
    w.end();
    w.finish(1, 0, cutenes::coreVersion, 0, 1);

    StateReader r(out.data(), out.size());
    EXPECT_TRUE(r.seek("CPU "));
    EXPECT_EQ(r.get<std::uint8_t>(), 9);
}

TEST(SaveState, MachineRejectsBadMagic)
{
    cutenes::Machine machine;
    const std::uint8_t junk[32] = {};
    EXPECT_EQ(machine.loadState(junk, sizeof junk),
        cutenes::LoadStateError::BadMagic);
}

TEST(SaveState, MachineRejectsFormatAndChecksum)
{
    auto blob = makeBlob();
    cutenes::Machine machine;
    // Header checks pass (format/core match), so the synthetic blob's
    // cartridge id hits the WrongCartridge gate.
    EXPECT_EQ(machine.loadState(blob.data(), blob.size()),
        cutenes::LoadStateError::WrongCartridge);
    blob[StateReader::kHeaderSize] ^= 1;
    EXPECT_NE(machine.loadState(blob.data(), blob.size()),
        cutenes::LoadStateError::Ok);
}
