// G010 (feature 003, research R3): the session's real-time mode, which the
// GUI drives. Expectations come from the research decision and the spec:
// - a frame runs for each one the host requests, and only while Running;
// - commands run between frames (002 FR-007, 003 FR-075);
// - live human input and automation input are combined at each frame start
//   (FR-031);
// - a scripted run is exact;
// - headless sessions never run frames on their own.

#include "support.hpp"

#include <cutenes/api/registry.hpp>
#include <cutenes/media.hpp>

#include <future>

#include <atomic>
#include <chrono>
#include <thread>

using namespace cutenes::api;
using namespace cutenes::api::test;
using cutenes::ButtonSet;
using cutenes::Port;

namespace {

constexpr const char* kRom = "instr_test-v5/rom_singles/01-basics.nes";

std::uint64_t frameOf(Session& s)
{
    return ok(s, "session.status")["frame"].get<std::uint64_t>();
}

// Polls until the session reaches `frame`, for at most 10 s.
bool reachFrame(Session& s, std::uint64_t frame)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < deadline) {
        if (frameOf(s) >= frame) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
}

ButtonSet buttons(std::initializer_list<cutenes::Button> held)
{
    ButtonSet b;
    for (auto button : held) b.set(static_cast<std::size_t>(button));
    return b;
}

Json heldOn(Session& s, int port)
{
    return ok(s, "core.get_controller", {{"port", port}})["buttons"];
}

// A running game in a real-time session, at frame 0.
void startRunning(Session& s)
{
    s.setRealtime(true);
    ok(s, "core.load_rom", {{"path", kRom}});
    ok(s, "core.resume");
}

// Registers gui.test_wait, which holds the emulation thread until released.
struct BlockingHost final : IHost {
    std::promise<void> started;
    std::promise<void> release;
    std::string mode() const override { return "gui"; }
    void onSessionEvent(const Event&) override {}
    void registerCommands(Registry& registry) override
    {
        Command wait;
        wait.name = "gui.test_wait";
        wait.description = "Block until the test releases it";
        wait.inputSchema = schema::object({});
        wait.outputSchema = schema::object({});
        wait.handler = [this](CallContext&, const Json&) {
            started.set_value();
            release.get_future().wait();
            return Result::ok();
        };
        registry.add(wait);
    }
};

struct CountingSinks final : cutenes::IVideoSink, cutenes::IAudioSink {
    std::atomic<int> frames{0};
    std::atomic<std::size_t> samples{0};
    void onFrame(const cutenes::VideoFrame&) override { ++frames; }
    void onSamples(const std::int16_t*, std::size_t count, std::uint64_t) override
    {
        samples += count;
    }
};

} // namespace

TEST(ApiRealtime, AHeadlessSessionRunsNoFramesOnItsOwn)
{
    Session s(testConfig());
    ok(s, "core.load_rom", {{"path", kRom}});
    ok(s, "core.resume");
    s.requestFrames(5); // not in real-time mode: ignored
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_EQ(frameOf(s), 0u);
}

TEST(ApiRealtime, EachRequestedFrameRunsOneFrame)
{
    Session s(testConfig());
    startRunning(s);
    s.requestFrames(5);
    ASSERT_TRUE(reachFrame(s, 5));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(frameOf(s), 5u) << "frames ran that nobody requested";
}

TEST(ApiRealtime, NothingRunsWhilePausedAndPendingFramesAreDropped)
{
    Session s(testConfig());
    s.setRealtime(true);
    ok(s, "core.load_rom", {{"path", kRom}}); // power-on leaves it Paused
    s.requestFrames(4);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(frameOf(s), 0u);
    ok(s, "core.resume");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(frameOf(s), 0u) << "frames requested while paused ran after resuming";
}

// A command queued behind pending frames runs before the rest of them, so
// the GUI's and automation's commands are never starved by play.
TEST(ApiRealtime, CommandsRunBetweenFrames)
{
    Session s(testConfig());
    startRunning(s);
    s.requestFrames(8);
    ok(s, "core.pause");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_LT(frameOf(s), 8u);
}

// The host asks for frames at each display refresh. If play falls behind,
// at most one request's worth of frames carries over, not an unbounded
// burst.
TEST(ApiRealtime, PendingFramesAreCappedAtTwiceTheRequest)
{
    BlockingHost host;
    SessionConfig config = testConfig();
    config.host = &host;
    Session s(config);
    startRunning(s);

    // Hold the emulation thread in a command, and request ten single frames
    // meanwhile.
    auto blocked = s.submit("gui.test_wait", Json::object());
    host.started.get_future().wait();
    for (int i = 0; i < 10; ++i) s.requestFrames(1);
    host.release.set_value();
    blocked.wait();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_EQ(frameOf(s), 2u);
}

TEST(ApiRealtime, LiveInputReachesTheGameAtTheNextFrameStart)
{
    Session s(testConfig());
    startRunning(s);
    s.setLiveInput(Port::One, buttons({cutenes::Button::A}));
    EXPECT_EQ(heldOn(s, 1), Json::array()) << "applied before a frame started";
    s.requestFrames(1);
    ASSERT_TRUE(reachFrame(s, 1));
    EXPECT_EQ(heldOn(s, 1), Json::array({"A"}));
    EXPECT_EQ(heldOn(s, 2), Json::array());
}

TEST(ApiRealtime, HumanAndAutomationInputAreCombined)
{
    Session s(testConfig());
    startRunning(s);
    ok(s, "core.set_controller", {{"port", 1}, {"buttons", {"B"}}});
    s.setLiveInput(Port::One, buttons({cutenes::Button::A}));
    s.requestFrames(1);
    ASSERT_TRUE(reachFrame(s, 1));
    const Json held = heldOn(s, 1);
    EXPECT_NE(std::find(held.begin(), held.end(), "A"), held.end()) << held.dump();
    EXPECT_NE(std::find(held.begin(), held.end(), "B"), held.end()) << held.dump();

    // Releasing the key leaves automation's button held.
    s.setLiveInput(Port::One, {});
    s.requestFrames(1);
    ASSERT_TRUE(reachFrame(s, 2));
    EXPECT_EQ(heldOn(s, 1), Json::array({"B"}));
}

// core.run_frames with a script is exact (002 FR-019): the script alone
// decides each frame's input, whatever a human is holding.
TEST(ApiRealtime, AScriptedRunIgnoresLiveInput)
{
    Session s(testConfig());
    startRunning(s);
    s.setLiveInput(Port::One, buttons({cutenes::Button::A}));
    ok(s, "core.run_frames", {{"frames", 2}, {"script", Json::array({Json::array({"Start"})})}});
    EXPECT_EQ(heldOn(s, 1), Json::array({"Start"}));
}

TEST(ApiRealtime, HostSinksReceiveEveryFrameAndItsAudio)
{
    Session s(testConfig());
    CountingSinks sinks;
    s.setMediaSinks(&sinks, &sinks);
    startRunning(s);
    s.requestFrames(3);
    ASSERT_TRUE(reachFrame(s, 3));
    ok(s, "core.run_frames", {{"frames", 2}}); // commands' frames reach them too
    EXPECT_EQ(sinks.frames.load(), 5);
    // 5 NTSC frames at 48 kHz are about 4000 samples.
    EXPECT_GT(sinks.samples.load(), 3500u);
    EXPECT_LT(sinks.samples.load(), 4500u);

    // The session's own capture still works (core.screen, core.audio).
    EXPECT_TRUE(ok(s, "core.frame_hash").contains("frameHash"));
    s.setMediaSinks(nullptr, nullptr);
    ok(s, "core.run_frames", {{"frames", 1}});
    EXPECT_EQ(sinks.frames.load(), 5);
}

TEST(ApiRealtime, LeavingRealtimeStopsPlay)
{
    Session s(testConfig());
    startRunning(s);
    s.setRealtime(false);
    s.requestFrames(3);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(frameOf(s), 0u);
}

// Feature 003 R6: the GUI's rewind runs each real-time frame itself, after
// live input is applied, to capture a state first or to play one back.
TEST(ApiRealtime, AFrameRunnerRunsEachRealtimeFrameInstead)
{
    Session s(testConfig());
    startRunning(s);
    std::atomic<int> calls{0};
    std::atomic<bool> inputSeen{false};
    s.setLiveInput(Port::One, buttons({cutenes::Button::A}));
    s.setFrameRunner([&](cutenes::Machine& machine) {
        ++calls;
        if (machine.controller(Port::One).test(static_cast<std::size_t>(cutenes::Button::A))) inputSeen = true;
        machine.runFrames(1);
    });
    s.requestFrames(3);
    ASSERT_TRUE(reachFrame(s, 3));
    EXPECT_EQ(calls.load(), 3);
    EXPECT_TRUE(inputSeen.load()) << "live input is applied before the runner";

    // A runner that runs nothing (rewind with no history) costs a frame of
    // time but advances nothing.
    s.setFrameRunner([&](cutenes::Machine&) { ++calls; });
    s.requestFrames(2);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_EQ(frameOf(s), 3u);

    s.setFrameRunner({}); // back to one plain frame
    s.requestFrames(2);
    ASSERT_TRUE(reachFrame(s, 5));
}
