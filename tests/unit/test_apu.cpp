#include "cutenes/apu/apu.hpp"

#include "cutenes/bus/bus.hpp"
#include "cutenes/system/scheduler.hpp"

#include <cutenes/cutenes.hpp>

#include <gtest/gtest.h>

#include <algorithm>
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
    h.apu.registerWrite(0x4000, 0x9F);          // duty 2, constant vol 15
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

// Issue #87. The output chain is the NES's (nesdev wiki "APU Mixer"):
// high-pass 90 Hz, high-pass 440 Hz, then low-pass 14 kHz, with no other
// gain. A steady ~4 kHz square wave (pulse timer 27: 1.789773 MHz / 16 / 28)
// sits well inside that band. A step of S through a one-pole high-pass with
// time constant tau settles to a peak-to-peak of 2S / (1 + e^(-T/(2 tau))),
// which for a 125 us half-period and 440 Hz (tau 362 us) is about 1.17 S.
// The 90 Hz stage adds a little and the 14 kHz low-pass rounds the edges,
// so expect 0.9 S to 1.4 S, where S is the mixer's value for one pulse at
// volume 15.
TEST(Apu, APulseToneKeepsItsMixerAmplitude)
{
    ApuHarness h;
    h.apu.registerWrite(0x4015, 0x01); // pulse 1 on
    h.apu.registerWrite(0x4000, 0xBF); // duty 2 (50%), length halt, constant volume 15
    h.apu.registerWrite(0x4002, 27);   // timer 27: about 3995 Hz
    h.apu.registerWrite(0x4003, 0x00);
    h.runFrames(6);
    ASSERT_GT(h.sink.samples.size(), 3200u);
    // The last two frames, once the filters have settled.
    const auto last = h.sink.samples.end() - 1600;
    const auto [lo, hi] = std::minmax_element(last, h.sink.samples.end());
    const double peakToPeak = static_cast<double>(*hi) - *lo;
    const double step = cutenes::Apu::mixerPulse(15);
    EXPECT_GT(peakToPeak, 0.9 * step) << "min " << *lo << ", max " << *hi << ", step " << step;
    EXPECT_LT(peakToPeak, 1.4 * step) << "min " << *lo << ", max " << *hi << ", step " << step;
    // The high-passes remove the DC: the tone is centred on zero.
    EXPECT_LT(std::abs(*hi + *lo), 0.2 * step);
}

TEST(Apu, LengthCounterExpiresAndStatusReflectsIt)
{
    ApuHarness h;
    h.apu.registerWrite(0x4015, 0x01);
    h.apu.registerWrite(0x4000, 0x9F);
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
    h.apu.registerWrite(0x4000, 0x9F);
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
        h.apu.registerWrite(0x4000, 0x9F);
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
    h2.apu.registerWrite(0x4000, 0x9F);
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

namespace {

// Blargg 2005 APU tests 10/11: a register write in the cycle of (or the one
// before) the second half-frame length clock is simultaneous with it; one
// cycle earlier than that is plainly before it. In 5-step mode the $4017
// write clocks length at once, so a length of 2 leaves 1 for the half-frame
// clock at +14913 cycles (after the 3-cycle restart delay).
struct HalfFrameCase {
    ApuHarness h;
    std::uint64_t clockAt = 0;

    HalfFrameCase(std::uint8_t halt)
    {
        h.apu.registerWrite(0x4015, 0x01);
        h.apu.registerWrite(0x4003, 0x18); // length 2
        h.apu.registerWrite(0x4000, halt);
        clockAt = h.scheduler.now();
        h.apu.registerWrite(0x4017, 0xC0);
        clockAt += (3 + 14913) * 12ull;
    }

    // Time-travel to `cycles` CPU cycles before the clock.
    void advanceToBefore(int cycles)
    {
        h.scheduler.advanceMaster(
            clockAt - cycles * 12ull - h.scheduler.now());
        h.apu.runTo(h.scheduler.now());
    }

    bool playing() { return (h.apu.registerRead(0x4015) & 1) != 0; }

    void finish()
    {
        h.scheduler.advanceMaster(20 * 12);
        h.apu.runTo(h.scheduler.now());
    }
};

} // namespace

TEST(ApuLength, HaltWrittenOneCycleBeforeClockStillClocks)
{
    HalfFrameCase c(0x10);
    c.advanceToBefore(1);
    c.h.apu.registerWrite(0x4000, 0x30); // halt
    c.finish();
    EXPECT_FALSE(c.playing());
}

TEST(ApuLength, HaltWrittenTwoCyclesBeforeClockPreventsIt)
{
    HalfFrameCase c(0x10);
    c.advanceToBefore(2);
    c.h.apu.registerWrite(0x4000, 0x30);
    c.finish();
    EXPECT_TRUE(c.playing());
}

TEST(ApuLength, UnhaltWrittenOneCycleBeforeClockDoesNotClock)
{
    HalfFrameCase c(0x30);
    c.advanceToBefore(1);
    c.h.apu.registerWrite(0x4000, 0x10); // unhalt
    c.finish();
    EXPECT_TRUE(c.playing());
}

TEST(ApuLength, ReloadDuringClockIsIgnoredWhileCounterRunning)
{
    HalfFrameCase c(0x10);
    c.advanceToBefore(1);
    c.h.apu.registerWrite(0x4003, 0x38); // would load 6
    // Ignored: the counter (1) was clocked to 0.
    c.finish();
    EXPECT_FALSE(c.playing());
}

TEST(ApuLength, ReloadDuringClockWorksWhenCounterIsZero)
{
    HalfFrameCase c(0x10);
    c.h.apu.registerWrite(0x4015, 0x00); // length -> 0
    c.h.apu.registerWrite(0x4015, 0x01);
    c.advanceToBefore(1);
    c.h.apu.registerWrite(0x4003, 0x18); // load 2, not decremented
    c.finish();
    EXPECT_TRUE(c.playing());
}

// --- PAL (nesdev "APU Noise", "APU DMC", "APU Frame Counter") ----------------

TEST(ApuPal, NoiseAndDmcTablesDifferFromNtscPerTheWiki)
{
    const auto& ntsc = cutenes::regionProfile(cutenes::Region::Ntsc);
    const auto& pal = cutenes::regionProfile(cutenes::Region::Pal);
    EXPECT_EQ(cutenes::Apu::noisePeriod(ntsc, 2), 16);
    EXPECT_EQ(cutenes::Apu::noisePeriod(pal, 2), 14);
    EXPECT_EQ(cutenes::Apu::noisePeriod(pal, 15), 3778);
    EXPECT_EQ(cutenes::Apu::noisePeriod(ntsc, 15), 4068);
    EXPECT_EQ(cutenes::Apu::dmcRate(ntsc, 0), 428);
    EXPECT_EQ(cutenes::Apu::dmcRate(pal, 0), 398);
    EXPECT_EQ(cutenes::Apu::dmcRate(pal, 15), 50);
    EXPECT_EQ(cutenes::Apu::dmcRate(ntsc, 15), 54);
}

namespace {

struct PalApuHarness {
    cutenes::Scheduler scheduler{cutenes::regionProfile(cutenes::Region::Pal)};
    cutenes::Bus bus;
    cutenes::Apu apu{cutenes::regionProfile(cutenes::Region::Pal), scheduler,
        bus};

    PalApuHarness()
    {
        apu.reset(/*cold=*/true);
        scheduler.reset();
    }

    void advanceCpuCycles(std::uint64_t cycles)
    {
        scheduler.advanceMaster(cycles * 16);
        apu.runTo(scheduler.now());
    }
};

} // namespace

// The PAL 4-step sequence is 33254 CPU cycles long (NTSC: 29830): the frame
// IRQ must not be visible at 29900 cycles but must be set shortly after 33254.
TEST(ApuPal, FourStepFrameIrqComesAtThePalPeriod)
{
    PalApuHarness h;
    h.apu.registerWrite(0x4017, 0x00);
    h.advanceCpuCycles(29900);
    EXPECT_EQ(h.apu.registerRead(0x4015) & 0x40, 0);
    h.advanceCpuCycles(33262 - 29900);
    EXPECT_NE(h.apu.registerRead(0x4015) & 0x40, 0);
}

// PAL half-frame length clocks at 16627 cycles (+3 restart delay in this
// model): a length-2 counter clocked by the $4017 mode-1 write is still
// playing at 16000 cycles and gone by 16700.
TEST(ApuPal, HalfFrameLengthClockIsAtCycle16627)
{
    PalApuHarness h;
    h.apu.registerWrite(0x4015, 0x01);
    h.apu.registerWrite(0x4003, 0x18); // length 2
    h.apu.registerWrite(0x4000, 0x10);
    h.apu.registerWrite(0x4017, 0xC0); // clocks length now: 1 left
    h.advanceCpuCycles(16000);
    EXPECT_NE(h.apu.registerRead(0x4015) & 1, 0);
    h.advanceCpuCycles(700);
    EXPECT_EQ(h.apu.registerRead(0x4015) & 1, 0);
}
