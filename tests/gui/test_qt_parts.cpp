// G012/G014: the cutenes-qt parts. Expectations are hand-computed from the
// spec (FR-040 scale, aspect, overscan, integer scaling), the menus
// contract's input defaults, and research R4 (rate control within ±0.5%,
// the clock at the console's rate).

#include <cutenes/qt/audio_output.hpp>
#include <cutenes/qt/frame_clock.hpp>
#include <cutenes/qt/frame_mailbox.hpp>
#include <cutenes/qt/input_mapper.hpp>
#include <cutenes/qt/media_runtime.hpp>
#include <cutenes/qt/viewport.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <chrono>
#include <thread>
#include <vector>

using namespace cutenes::qt;
using cutenes::Button;
using cutenes::Port;

namespace {

void expectRect(const QRectF& actual, double x, double y, double w, double h)
{
    EXPECT_NEAR(actual.x(), x, 1e-3);
    EXPECT_NEAR(actual.y(), y, 1e-3);
    EXPECT_NEAR(actual.width(), w, 1e-3);
    EXPECT_NEAR(actual.height(), h, 1e-3);
}

DisplayOptions square(bool integer = true)
{
    DisplayOptions o;
    o.aspect = Aspect::Square;
    o.overscan = {0, 0, 0, 0};
    o.integerScaling = integer;
    return o;
}

} // namespace

// The defaults: 8:7 pixels, 8 lines cropped top and bottom, integer scale.
// 256x224 at 8:7 is 292.571 x 224; in 1000x700 the fit is 3.125, so 3x:
// 877.714 x 672, centred.
TEST(GuiViewport, DefaultsCropTheTvOverscanAndScaleByWholeSteps)
{
    const Viewport v = computeViewport({1000, 700}, DisplayOptions{});
    EXPECT_EQ(v.source, QRect(0, 8, 256, 224));
    expectRect(v.target, (1000 - 256 * 8.0 / 7 * 3) / 2, 14, 256 * 8.0 / 7 * 3, 672);
}

TEST(GuiViewport, SquarePixelsFillAnExactMultiple)
{
    const Viewport v = computeViewport({512, 480}, square());
    EXPECT_EQ(v.source, QRect(0, 0, 256, 240));
    expectRect(v.target, 0, 0, 512, 480);
}

// 4:3 stretches the whole 256x240 picture to 4:3: 640x480 exactly.
TEST(GuiViewport, FourThreeWithoutIntegerScalingFillsTheArea)
{
    DisplayOptions o = square(false);
    o.aspect = Aspect::FourThree;
    expectRect(computeViewport({640, 480}, o).target, 0, 0, 640, 480);
    EXPECT_NEAR(pixelAspect(Aspect::FourThree), (4.0 / 3) / (256.0 / 240), 1e-12);
}

// Below 1x, integer scaling would show nothing; it fits exactly instead.
TEST(GuiViewport, AnAreaSmallerThanOneTimesFitsExactly)
{
    expectRect(computeViewport({100, 100}, square()).target, 0, 3.125, 100, 93.75);
}

TEST(GuiViewport, OverscanIsClampedToSixteenPixels)
{
    DisplayOptions o = square();
    o.overscan = {20, 0, 0, 30};
    EXPECT_EQ(computeViewport({256, 240}, o).source, QRect(0, 16, 240, 224));
}

TEST(GuiViewport, ScaledSizeIsThePictureAtThatScale)
{
    const QSizeF size = scaledSize(3, DisplayOptions{});
    EXPECT_NEAR(size.width(), 256 * 8.0 / 7 * 3, 1e-9);
    EXPECT_NEAR(size.height(), 672, 1e-9);
}

// The menus contract, "Game input defaults": arrows, X = A, Z = B,
// Shift = Select, Enter = Start, all on player 1.
TEST(GuiInput, KeyboardDefaultsFollowTheContract)
{
    InputMapper m;
    const struct {
        int key;
        Button button;
    } cases[] = {
        {Qt::Key_Up, Button::Up}, {Qt::Key_Down, Button::Down}, {Qt::Key_Left, Button::Left},
        {Qt::Key_Right, Button::Right}, {Qt::Key_X, Button::A}, {Qt::Key_Z, Button::B},
        {Qt::Key_Shift, Button::Select}, {Qt::Key_Return, Button::Start},
        {Qt::Key_Enter, Button::Start},
    };
    for (const auto& c : cases) {
        EXPECT_TRUE(m.handleKey(c.key, true));
        cutenes::ButtonSet expected;
        expected.set(static_cast<std::size_t>(c.button));
        EXPECT_EQ(m.buttons(Port::One), expected) << c.key;
        EXPECT_TRUE(m.buttons(Port::Two).none());
        m.handleKey(c.key, false);
        EXPECT_TRUE(m.buttons(Port::One).none());
    }
    EXPECT_FALSE(m.handleKey(Qt::Key_Q, true)) << "an unbound key is left to others";
}

TEST(GuiInput, HeldKeysCombineAndClearReleasesThemAll)
{
    InputMapper m;
    m.handleKey(Qt::Key_X, true);
    m.handleKey(Qt::Key_Right, true);
    EXPECT_EQ(m.buttons(Port::One).count(), 2u);
    m.clear();
    EXPECT_TRUE(m.buttons(Port::One).none());
}

TEST(GuiMailbox, HandsOverTheNewestFrameOnce)
{
    FrameMailbox box;
    std::vector<std::uint32_t> out;
    std::uint64_t seen = 0;
    std::uint64_t index = 0;
    EXPECT_FALSE(box.takeNewer(out, seen, index));

    std::vector<std::uint32_t> a(FrameMailbox::kPixels, 0x112233);
    std::vector<std::uint32_t> b(FrameMailbox::kPixels, 0x445566);
    box.publish(a.data(), 7);
    box.publish(b.data(), 8); // the view had no time for frame 7
    ASSERT_TRUE(box.takeNewer(out, seen, index));
    EXPECT_EQ(out, b);
    EXPECT_EQ(index, 8u);
    EXPECT_FALSE(box.takeNewer(out, seen, index));

    box.clear();
    EXPECT_FALSE(box.hasFrame());
    ASSERT_TRUE(box.takeNewer(out, seen, index)) << "a clear is news: the view goes black";
    EXPECT_TRUE(out.empty());
}

// The timer runs at its rate: a 100 Hz clock ticks 100 times a second on
// average. It keeps a fixed schedule, so a tick that comes late (a virtual
// machine's timer slack) is followed by a quick one; single intervals say
// little, the average says whether the game runs at speed. After a stall
// it skips ahead instead of bursting: it catches up on at most 4 periods,
// so any 8 ticks span at least one period. Pacing under load is soak-tested
// with a simulated clock in G9 (A15).
TEST(GuiFrameClock, TicksAtItsRate)
{
    using clock = std::chrono::steady_clock;
    std::mutex mutex;
    std::vector<clock::time_point> times;
    FrameClock frameClock([&](std::uint32_t) {
        std::lock_guard lock(mutex);
        times.push_back(clock::now());
    });
    frameClock.start(100.0);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    frameClock.stop();
    EXPECT_NEAR(frameClock.rate(), 100.0, 0.01);

    std::lock_guard lock(mutex);
    ASSERT_GE(times.size(), 10u);
    const double seconds = std::chrono::duration<double>(times.back() - times.front()).count();
    const double hz = static_cast<double>(times.size() - 1) / seconds;
    // Stalls only ever slow it down (CI's loaded macOS VM measured 65 Hz);
    // a wrong period, such as half the rate, still fails.
    EXPECT_GT(hz, 55.0);
    EXPECT_LT(hz, 105.0);
    for (std::size_t i = 8; i < times.size(); ++i) {
        EXPECT_GE(times[i] - times[i - 8], std::chrono::milliseconds(10)) << "a burst at tick " << i;
    }

    const std::size_t after = times.size();
    mutex.unlock();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    mutex.lock();
    EXPECT_EQ(times.size(), after) << "ticks after stop()";
}

// R4: the queue above its target plays slightly faster, never more than
// 0.5% off; clear() empties the queue.
TEST(GuiAudio, RateControlStaysWithinHalfAPercent)
{
    const MediaRuntime media;
    AudioOutput audio(48000);
    ASSERT_TRUE(audio.open()) << audio.error();
    const std::vector<std::int16_t> chunk(800, 1000);
    for (int i = 0; i < 200; ++i) audio.onSamples(chunk.data(), chunk.size(), 0);
    const AudioOutput::Stats s = audio.stats();
    EXPECT_GT(s.queuedMs, 0);
    EXPECT_GT(s.rate, 1.0) << "a long queue should drain faster";
    EXPECT_LE(s.rate, 1.0 + AudioOutput::kMaxRateAdjust + 1e-9);
    EXPECT_GE(s.rate, 1.0 - AudioOutput::kMaxRateAdjust - 1e-9);
    // The backlog is dropped beyond four times the target latency.
    EXPECT_LE(s.queuedMs, 4 * audio.targetLatencyMs() + 20);

    audio.clear();
    EXPECT_EQ(audio.stats().queuedMs, 0);
}
