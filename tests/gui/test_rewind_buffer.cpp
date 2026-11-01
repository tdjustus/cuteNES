// G032 (A6): the rewind buffer (research R6, data-model "RewindBuffer").
// It must hand every state back byte for byte, newest first, keep within its
// budget by dropping the oldest, and hold at least 30 s of a real game's
// states in the default 64 MiB (FR-034).

#include "persistence/rewind_buffer.hpp"

#include <cutenes/machine.hpp>

#include <QFile>

#include <gtest/gtest.h>

#include <cstdio>
#include <random>
#include <vector>

using cutenes::ButtonSet;
using cutenes::app::RewindBuffer;

namespace {

using State = std::vector<std::uint8_t>;

// States that change a little each frame, as a game's do.
std::vector<State> frames(int count, std::size_t size = 4096, unsigned seed = 7)
{
    std::mt19937 random(seed);
    std::vector<State> states;
    State state(size);
    for (auto& b : state) b = static_cast<std::uint8_t>(random());
    for (int i = 0; i < count; ++i) {
        for (int k = 0; k < 20; ++k) state[random() % size] = static_cast<std::uint8_t>(random());
        states.push_back(state);
    }
    return states;
}

void push(RewindBuffer& buffer, const State& state, int input = 0)
{
    buffer.push(state.data(), state.size(), ButtonSet(static_cast<unsigned long>(input)), ButtonSet());
}

} // namespace

TEST(GuiRewindBuffer, GivesEveryStateBackNewestFirst)
{
    RewindBuffer buffer(64u << 20);
    const auto states = frames(300);
    for (std::size_t i = 0; i < states.size(); ++i) push(buffer, states[i], static_cast<int>(i % 256));
    EXPECT_EQ(buffer.size(), 300u);
    for (std::size_t i = states.size(); i-- > 0;) {
        const auto entry = buffer.pop();
        ASSERT_TRUE(entry.has_value()) << i;
        EXPECT_EQ(entry->state, states[i]) << i;
        EXPECT_EQ(entry->input[0].to_ulong(), i % 256) << "each frame's input comes back with its state";
    }
    EXPECT_FALSE(buffer.pop().has_value());
    EXPECT_TRUE(buffer.empty());
}

TEST(GuiRewindBuffer, PushingAfterPoppingContinuesTheHistory)
{
    RewindBuffer buffer(64u << 20);
    const auto states = frames(50);
    for (int i = 0; i < 30; ++i) push(buffer, states[i]);
    for (int i = 0; i < 10; ++i) buffer.pop();
    for (int i = 30; i < 50; ++i) push(buffer, states[i]);
    for (int i = 49; i >= 30; --i) EXPECT_EQ(buffer.pop()->state, states[i]);
    for (int i = 19; i >= 0; --i) EXPECT_EQ(buffer.pop()->state, states[i]);
}

TEST(GuiRewindBuffer, KeepsWithinItsBudgetByDroppingTheOldest)
{
    const auto states = frames(2000);
    RewindBuffer buffer(64u << 10); // 64 KiB: about 1000 small deltas
    for (const auto& s : states) push(buffer, s);
    EXPECT_LE(buffer.bytes(), 64u << 10);
    EXPECT_LT(buffer.size(), 2000u);
    EXPECT_GT(buffer.size(), 100u) << "deltas are small";
    // What is left is the newest states, still exact.
    const std::size_t kept = buffer.size();
    for (std::size_t i = 0; i < kept; ++i) {
        EXPECT_EQ(buffer.pop()->state, states[states.size() - 1 - i]) << i;
    }
}

TEST(GuiRewindBuffer, AZeroBudgetKeepsNothing)
{
    RewindBuffer buffer(0);
    push(buffer, frames(1)[0]);
    EXPECT_TRUE(buffer.empty());
}

TEST(GuiRewindBuffer, AStateOfAnotherSizeStartsOver)
{
    RewindBuffer buffer(64u << 20);
    push(buffer, frames(1, 4096)[0]);
    const State other = frames(1, 5000, 9)[0];
    push(buffer, other);
    EXPECT_EQ(buffer.size(), 1u);
    EXPECT_EQ(buffer.pop()->state, other);
}

TEST(GuiRewindBuffer, ClearEmptiesIt)
{
    RewindBuffer buffer(64u << 20);
    for (const auto& s : frames(10)) push(buffer, s);
    buffer.clear();
    EXPECT_TRUE(buffer.empty());
    EXPECT_EQ(buffer.bytes(), 0u);
}

// FR-034: the default budget holds at least 30 s. Measured on real states
// of a test ROM running (its CPU RAM, PPU and APU change every frame).
TEST(GuiRewindBuffer, TheDefaultBudgetHoldsThirtySecondsOfARealGame)
{
    QFile rom(QStringLiteral(CUTENES_TEST_ROMS "/instr_test-v5/official_only.nes"));
    ASSERT_TRUE(rom.open(QIODevice::ReadOnly));
    const QByteArray bytes = rom.readAll();
    cutenes::Machine machine;
    ASSERT_EQ(machine.loadCartridge(reinterpret_cast<const std::uint8_t*>(bytes.constData()),
                  static_cast<std::size_t>(bytes.size())).error,
        cutenes::LoadError::Ok);
    machine.resume();
    RewindBuffer buffer(RewindBuffer::kDefaultBudget);
    constexpr int kThirtySeconds = 30 * 60;
    std::size_t stateSize = 0;
    for (int i = 0; i < kThirtySeconds; ++i) {
        const cutenes::SaveStateBlob blob = machine.captureState();
        stateSize = blob.size;
        buffer.push(blob.data, blob.size, {}, {});
        machine.runFrames(1);
    }
    EXPECT_EQ(buffer.size(), static_cast<std::size_t>(kThirtySeconds)) << "nothing dropped";
    std::printf("[ rewind ] state %zu bytes; 30 s of history uses %zu bytes (%.1f KiB/s)\n", stateSize,
        buffer.bytes(), static_cast<double>(buffer.bytes()) / 30.0 / 1024.0);
    EXPECT_LT(buffer.bytes(), RewindBuffer::kDefaultBudget / 4) << "with room for larger states";
}
