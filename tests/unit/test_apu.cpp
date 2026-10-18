#include "cutenes/apu/apu.hpp"

#include "cutenes/bus/bus.hpp"
#include "cutenes/system/scheduler.hpp"

#include <cutenes/cutenes.hpp>

#include <gtest/gtest.h>

#include <cmath>

namespace {

struct Sink final : cutenes::IAudioSink {
    void onSamples(const std::int16_t* pcm, std::size_t count,
        std::uint64_t) override
    {
        samples.insert(samples.end(), pcm, pcm + count);
    }
    std::vector<std::int16_t> samples;
};

struct ApuHarness {
    cutenes::Scheduler scheduler{cutenes::regionProfile(cutenes::Region::Ntsc)};
    cutenes::Bus bus;
    cutenes::Apu apu{cutenes::regionProfile(cutenes::Region::Ntsc), scheduler,
        bus};
    Sink sink;

    ApuHarness()
    {
        apu.setOutputRate(48000);
        apu.setAudioSink(&sink);
        apu.reset(/*cold=*/true);
        scheduler.reset();
    }

    void runFrames(int frames)
    {
        const std::uint64_t ticksPerFrame =
            cutenes::regionProfile(cutenes::Region::Ntsc).dots
            * cutenes::regionProfile(cutenes::Region::Ntsc).scanlines * 4;
        for (int i = 0; i < frames; ++i) {
            scheduler.advanceMaster(ticksPerFrame);
            apu.runTo(scheduler.now());
            apu.flushToSink();
        }
    }
};

} // namespace

TEST(ApuMixer, TablesAreMonotonicAndSilentAtZero)
{
    EXPECT_EQ(cutenes::Apu::mixerPulse(0), 0);
    EXPECT_GT(cutenes::Apu::mixerPulse(30), cutenes::Apu::mixerPulse(15));
    EXPECT_GT(cutenes::Apu::mixerPulse(15), cutenes::Apu::mixerPulse(1));
    EXPECT_EQ(cutenes::Apu::mixerTnd(0), 0);
    EXPECT_GT(cutenes::Apu::mixerTnd(200), cutenes::Apu::mixerTnd(100));
}

TEST(Apu, SilenceProducesZeroOutput)
{
    ApuHarness h;
    h.runFrames(2);
    // Filters may ring slightly from exact zero input? Zero in -> zero out.
    for (const std::int16_t s : h.sink.samples) {
        EXPECT_EQ(s, 0);
    }
    EXPECT_GT(h.sink.samples.size(), 800u); // ~1.6 frames x ~800 samples
}

TEST(Apu, EnabledPulseProducesSound)
{
    ApuHarness h;
    h.apu.registerWrite(0x4015, 0x01);          // pulse 1 on
    h.apu.registerWrite(0x4000, 0xBF);          // duty 2, constant vol 15
    h.apu.registerWrite(0x4002, 0x00);          // timer lo
    h.apu.registerWrite(0x4003, 0x01);          // timer hi=1, length idx 0 (10)
    h.runFrames(1);
    bool anyNonZero = false;
    for (const std::int16_t s : h.sink.samples) {
        if (s != 0) {
            anyNonZero = true;
            break;
        }
    }
    EXPECT_TRUE(anyNonZero);
}

TEST(Apu, LengthCounterExpiresAndStatusReflectsIt)
{
    ApuHarness h;
    h.apu.registerWrite(0x4015, 0x01);
    h.apu.registerWrite(0x4000, 0xBF);
    h.apu.registerWrite(0x4002, 0x00);
    h.apu.registerWrite(0x4003, 0x00); // length index 0 -> 10 ticks
    EXPECT_NE(h.apu.registerRead(0x4015) & 0x01, 0); // still counting

    // Length decrements on half frames: 10 ticks -> 10 half frames
    // (~2.5 frames); 6 frames is safely past expiry.
    h.runFrames(6);
    EXPECT_EQ(h.apu.registerRead(0x4015) & 0x01, 0);
}

TEST(Apu, FrameIrqFiresInFourStepModeAndReadingClears)
{
    ApuHarness h;
    h.runFrames(2); // sequencer free-running, not inhibited
    EXPECT_TRUE(h.apu.frameIrqPending());
    (void)h.apu.registerRead(0x4015);
    EXPECT_FALSE(h.apu.frameIrqPending());
}

TEST(Apu, OutputRateRatioMatchesNativeRate)
{
    ApuHarness h;
    h.apu.registerWrite(0x4015, 0x01);
    h.apu.registerWrite(0x4000, 0xBF);
    h.apu.registerWrite(0x4002, 0x00);
    h.apu.registerWrite(0x4003, 0x01);
    h.runFrames(60); // ~1 second of emulated time
    // NTSC native APU rate = 1789773/2 = 894886.4 Hz; 48 kHz output expects
    // ~894886/48000 * frames... i.e. ~14890 native vs ~798 out per frame.
    const double expected = 60.0 * 14890.0 * 48000.0 / 894886.0;
    const double actual = static_cast<double>(h.sink.samples.size());
    EXPECT_NEAR(actual, expected, expected * 0.02); // within 2%
}

TEST(Apu, DeterministicAcrossRuns)
{
    std::vector<std::int16_t> first;
    {
        ApuHarness h;
        h.apu.registerWrite(0x4015, 0x0F);
        h.apu.registerWrite(0x4000, 0xBF);
        h.apu.registerWrite(0x4002, 0x33);
        h.apu.registerWrite(0x4003, 0x01);
        h.apu.registerWrite(0x4008, 0x81);
        h.apu.registerWrite(0x400A, 0x54);
        h.apu.registerWrite(0x400B, 0x04);
        h.apu.registerWrite(0x400C, 0x8F);
        h.apu.registerWrite(0x400E, 0x06);
        h.apu.registerWrite(0x400F, 0x08);
        h.runFrames(5);
        first = h.sink.samples;
    }
    ApuHarness h2;
    h2.apu.registerWrite(0x4015, 0x0F);
    h2.apu.registerWrite(0x4000, 0xBF);
    h2.apu.registerWrite(0x4002, 0x33);
    h2.apu.registerWrite(0x4003, 0x01);
    h2.apu.registerWrite(0x4008, 0x81);
    h2.apu.registerWrite(0x400A, 0x54);
    h2.apu.registerWrite(0x400B, 0x04);
    h2.apu.registerWrite(0x400C, 0x8F);
    h2.apu.registerWrite(0x400E, 0x06);
    h2.apu.registerWrite(0x400F, 0x08);
    h2.runFrames(5);
    EXPECT_EQ(first, h2.sink.samples); // byte-identical (FR-012)
}
