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

namespace {

// NROM-128 whose reset vector points at JMP $C000: enough to run frames.
std::vector<std::uint8_t> makeLoopRom()
{
    std::vector<std::uint8_t> rom{'N', 'E', 'S', 0x1A, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    rom.resize(16 + 16384, 0);
    const std::uint8_t loop[] = {0x4C, 0x00, 0xC0}; // JMP $C000
    std::copy(std::begin(loop), std::end(loop), rom.begin() + 16);
    rom[16 + 0x3FFC] = 0x00;
    rom[16 + 0x3FFD] = 0xC0;
    return rom;
}

// A state captured 10 frames in, copied out of the core's buffer.
std::vector<std::uint8_t> capturedState(cutenes::Machine& machine, const std::vector<std::uint8_t>& rom)
{
    EXPECT_EQ(machine.loadCartridge(rom.data(), rom.size()).error, cutenes::LoadError::Ok);
    machine.powerOn();
    machine.runFrames(10);
    const cutenes::SaveStateBlob view = machine.captureState();
    return {view.data, view.data + view.size};
}

void patchU64(std::vector<std::uint8_t>& blob, std::size_t at, std::uint64_t value)
{
    for (std::size_t i = 0; i < 8; ++i) blob[at + i] = static_cast<std::uint8_t>(value >> (8 * i));
}

} // namespace

// #166, save-state-format.md "Validation": the format version decides
// whether a state loads. The core version that wrote it is information
// only, so a newer core still loads an older core's states.
TEST(SaveState, AStateFromAnotherCoreVersionLoads)
{
    const auto rom = makeLoopRom();
    cutenes::Machine machine;
    auto blob = capturedState(machine, rom);
    // The header's coreVersion field (offset 8) is outside the checksum,
    // which covers the sections only.
    patchU64(blob, 8, (std::uint64_t{0} << 22) | (std::uint64_t{1} << 12)); // 0.1.0
    ASSERT_NE(StateReader(blob.data(), blob.size()).coreVersion(), cutenes::coreVersion);
    machine.runFrames(5);
    EXPECT_EQ(machine.loadState(blob.data(), blob.size()), cutenes::LoadStateError::Ok);
    EXPECT_EQ(machine.frameCounter(), 10u);
}

TEST(SaveState, AnotherFormatVersionIsRefused)
{
    const auto rom = makeLoopRom();
    cutenes::Machine machine;
    auto blob = capturedState(machine, rom);
    blob[4] = 2; // formatVersion (u16 at offset 4)
    blob[5] = 0;
    EXPECT_EQ(machine.loadState(blob.data(), blob.size()), cutenes::LoadStateError::IncompatibleFormat);
}
