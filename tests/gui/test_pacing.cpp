// G092, the pacing soak (quickstart A15; spec FR-030, SC-004; research R4).
// Ten minutes of play, 36,000 frames, on a simulated clock: the real core
// makes each frame and its sound (emulated once per region, then replayed
// frame by frame through each display: how much sound a frame makes does
// not depend on the display), the real choosePacing picks the clock,
// the real AudioPacer steers the sound queue, and frames go through the real
// FrameMailbox. Only time is simulated: the frame clock's ticks (with timer
// jitter), the display's vsyncs, and a sound card that takes samples in
// 10 ms blocks at its own, slightly wrong, clock. That makes the run
// deterministic and as fast as the core, and it can model what a real
// machine only shows over minutes: two crystals drifting apart.
//
// For each display (60 Hz, 59.94 Hz, 120 Hz, PAL at 50 Hz, and 144 Hz,
// which cannot sync):
// - the sound never runs dry once it has started: the queue never falls
//   below one of the card's blocks, nor grows past twice its target;
// - the playback rate stays within ±0.5% (R4), and settles on the ratio of
//   the rate the sound is made at to the rate the card takes it;
// - synced to the display, each vsync that is due a frame shows the next
//   one: none dropped, none repeated (SC-004). At 144 Hz, synced to sound,
//   only the sound is checked: the display shows the newest frame.
// Expectations come from R4 and the console's rates (nesdev "Cycle
// reference chart": NTSC 60.0988 Hz, PAL 50.0070 Hz), not from a run.
//
// A Release test: in a Debug build the core is too slow for 72,000 frames,
// so it is skipped there.

#include <cutenes/machine.hpp>
#include <cutenes/media.hpp>
#include <cutenes/qt/audio_pacer.hpp>
#include <cutenes/qt/frame_clock.hpp>
#include <cutenes/qt/frame_mailbox.hpp>

#include <QFile>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

using cutenes::Machine;
using cutenes::Region;
using cutenes::qt::AudioPacer;
using cutenes::qt::FrameMailbox;
using cutenes::qt::Pacing;
using cutenes::qt::PacingMode;

namespace {

constexpr double kNtscHz = 60.0988;
constexpr double kPalHz = 50.0070;
constexpr int kSampleRate = 48000;      // the core's output rate and the card's nominal one
constexpr int kTargetMs = 64;           // audio.latencyMs's default
constexpr int kBlock = kSampleRate / 100; // the card takes 10 ms at a time
constexpr int kFrames = 36000;          // ten minutes at 60 Hz

struct Scenario {
    const char* name;
    double displayHz;
    double cardSkew; // the card's clock against nominal: +0.0002 runs 0.02% fast
    Region region;
};

struct Outcome {
    Pacing pacing;
    std::uint64_t underruns = 0; // a block the card found short, once playing
    bool started = false;
    std::uint64_t dropped = 0;  // a due vsync that skipped a frame
    std::uint64_t repeated = 0; // a due vsync with no new frame
    double minRate = 1, maxRate = 1, finalRate = 1;
    int maxQueuedAfterSettle = 0, minQueuedAfterSettle = 1 << 30;
    std::uint64_t samples = 0; // made by the core
};

// The sound queue, fed as AudioOutput feeds it.
struct SoundQueue {
    AudioPacer pacer;
    double queued = 0; // samples waiting (fractional: the rate ratio consumes fractions)
    void put(int count)
    {
        const int target = kSampleRate * kTargetMs / 1000;
        const int before = static_cast<int>(queued);
        queued += count;
        if (pacer.queued(before, static_cast<int>(queued), target).dropBacklog) queued = count;
    }
};

// The samples each frame makes, from the real core playing nestest.
struct Counter final : cutenes::IAudioSink {
    std::vector<int>* frames = nullptr;
    int pending = 0;
    void onSamples(const std::int16_t*, std::size_t count, std::uint64_t) override { pending += static_cast<int>(count); }
};

std::vector<int> samplesPerFrame(Region region)
{
    QFile file(QStringLiteral(CUTENES_TEST_ROMS "/other/nestest.nes"));
    EXPECT_TRUE(file.open(QIODevice::ReadOnly));
    const QByteArray bytes = file.readAll();
    Machine machine;
    machine.setRegionOverride(region);
    EXPECT_EQ(machine.loadCartridge(reinterpret_cast<const std::uint8_t*>(bytes.constData()), bytes.size()).error,
        cutenes::LoadError::Ok);
    machine.powerOn();
    Counter counter;
    machine.setAudioSink(&counter);
    std::vector<int> out;
    out.reserve(kFrames);
    for (int i = 0; i < kFrames; ++i) {
        machine.runFrames(1);
        out.push_back(counter.pending);
        counter.pending = 0;
    }
    return out;
}

Outcome soak(const Scenario& scenario, const std::vector<int>& samples)
{
    SoundQueue sound;
    FrameMailbox mailbox;
    const std::vector<std::uint32_t> picture(FrameMailbox::kPixels);

    const double consoleHz = scenario.region == Region::Pal ? kPalHz : kNtscHz;
    Outcome out;
    out.pacing = cutenes::qt::choosePacing(PacingMode::Auto, scenario.displayHz, consoleHz);
    const double tickPeriod = 1.0 / out.pacing.framesPerSecond;
    const double vsyncPeriod = 1.0 / scenario.displayHz;
    const double blockPeriod = static_cast<double>(kBlock) / (kSampleRate * (1.0 + scenario.cardSkew));

    // The clock keeps a fixed schedule half a vsync from the display's,
    // each tick up to 2 ms late (a loaded machine's timer slack).
    std::mt19937 random(12345);
    std::uniform_real_distribution<double> slack(0.0, 0.002);
    double nextTick = vsyncPeriod / 2 + slack(random);
    double nextVsync = vsyncPeriod;
    double nextBlock = blockPeriod;
    int ticks = 0;
    std::uint64_t vsyncs = 0;
    std::uint64_t seen = 0, shown = 0;
    std::vector<std::uint32_t> frame;
    const double settle = 10.0; // seconds for the rate to settle
    const int target = kSampleRate * kTargetMs / 1000;

    while (ticks < kFrames) {
        const double now = std::min({nextTick, nextVsync, nextBlock});
        if (now == nextBlock) {
            // The card takes a block, at its rate times the stream's ratio.
            if (sound.pacer.playing()) {
                out.started = true;
                const double wanted = kBlock * sound.pacer.rate();
                if (sound.queued < wanted) {
                    ++out.underruns;
                    sound.queued = 0;
                } else {
                    sound.queued -= wanted;
                }
                out.minRate = std::min(out.minRate, sound.pacer.rate());
                out.maxRate = std::max(out.maxRate, sound.pacer.rate());
                if (now > settle) {
                    out.maxQueuedAfterSettle = std::max(out.maxQueuedAfterSettle, static_cast<int>(sound.queued));
                    out.minQueuedAfterSettle = std::min(out.minQueuedAfterSettle, static_cast<int>(sound.queued));
                }
            }
            nextBlock += blockPeriod;
        } else if (now == nextTick) {
            // The frame's sound, then its picture, as the session hands them on.
            sound.put(samples[static_cast<std::size_t>(ticks)]);
            out.samples += static_cast<std::uint64_t>(samples[static_cast<std::size_t>(ticks)]);
            mailbox.publish(picture.data(), static_cast<std::uint64_t>(ticks) + 1);
            ++ticks;
            nextTick = (ticks + 0.5) * tickPeriod + slack(random); // the schedule, not the last tick
            nextTick = std::max(nextTick, now);
        } else {
            // A vsync shows the newest frame; with divisor N, a frame is
            // due at every Nth.
            ++vsyncs;
            std::uint64_t index = shown;
            const bool fresh = mailbox.takeNewer(frame, seen, index);
            const bool due = out.pacing.display && vsyncs % static_cast<std::uint64_t>(out.pacing.divisor) == 0;
            if (due && vsyncs > 2) {
                if (!fresh) ++out.repeated;
                else if (index > shown + 1) out.dropped += index - shown - 1;
            }
            if (fresh) shown = index;
            nextVsync += vsyncPeriod;
        }
    }
    out.finalRate = sound.pacer.rate();
    (void)target;
    return out;
}

void check(const Scenario& scenario, const std::vector<int>& samples, bool display, int divisor)
{
    const Outcome out = soak(scenario, samples);
    SCOPED_TRACE(scenario.name);
    const double consoleHz = scenario.region == Region::Pal ? kPalHz : kNtscHz;
    EXPECT_EQ(out.pacing.display, display);
    EXPECT_EQ(out.pacing.divisor, divisor);

    // The core makes 48,000 samples per emulated second, about one frame's
    // worth (800) per frame.
    EXPECT_NEAR(static_cast<double>(out.samples) / kFrames, kSampleRate / consoleHz, 1.0);

    EXPECT_TRUE(out.started);
    EXPECT_EQ(out.underruns, 0u);
    const int target = kSampleRate * kTargetMs / 1000;
    EXPECT_LE(out.maxQueuedAfterSettle, 2 * target);
    // Above one block: the card's next request is always met. (Where it
    // settles below the target is the control's steady error, which grows
    // with the rate correction the clocks need.)
    EXPECT_GE(out.minQueuedAfterSettle, kBlock);

    EXPECT_GE(out.minRate, 1.0 - AudioPacer::kMaxRateAdjust - 1e-9);
    EXPECT_LE(out.maxRate, 1.0 + AudioPacer::kMaxRateAdjust + 1e-9);
    // Settled: sound made at (frames per second x samples per frame), taken
    // at the card's rate. The ratio is that quotient, within the control's
    // small steady error.
    const double made = out.pacing.framesPerSecond * kSampleRate / consoleHz;
    const double taken = kSampleRate * (1.0 + scenario.cardSkew);
    EXPECT_NEAR(out.finalRate, made / taken, 0.0005);

    if (display) {
        EXPECT_EQ(out.dropped, 0u);
        EXPECT_EQ(out.repeated, 0u);
    }
}

} // namespace

class GuiPacing : public ::testing::Test {
protected:
    void SetUp() override
    {
#ifndef NDEBUG
        GTEST_SKIP() << "the pacing soak runs in Release builds (quickstart A15)";
#endif
    }
};

TEST_F(GuiPacing, TenMinutesOfNtscOnEachDisplay)
{
    const std::vector<int> samples = samplesPerFrame(Region::Ntsc);
    check({"60 Hz, card 0.02% fast", 60.0, +0.0002, Region::Ntsc}, samples, true, 1);
    check({"59.94 Hz, card 0.05% slow", 59.94, -0.0005, Region::Ntsc}, samples, true, 1);
    check({"120 Hz: each frame for two vsyncs", 120.0, +0.0003, Region::Ntsc}, samples, true, 2);
    check({"144 Hz: synced to sound", 144.0, +0.0004, Region::Ntsc}, samples, false, 1);
}

TEST_F(GuiPacing, TenMinutesOfPalOnAFiftyHertzDisplay)
{
    const std::vector<int> samples = samplesPerFrame(Region::Pal);
    check({"PAL 50 Hz, card 0.02% slow", 50.0, -0.0002, Region::Pal}, samples, true, 1);
}
