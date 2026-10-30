// Q8 serialization (tasks A019, FR-007): calls from many threads run one at
// a time in a single total order, and no result sees a torn state.

#include "support.hpp"

#include <algorithm>
#include <mutex>
#include <thread>

using namespace cutenes::api;
using namespace cutenes::api::test;

TEST(ApiConcurrency, InterleavedCallsFormOneTotalOrder)
{
    Session s(testConfig());
    ok(s, "core.load_rom", {{"path", "instr_test-v5/rom_singles/01-basics.nes"}});

    constexpr int kThreads = 6;
    constexpr int kCalls = 20;
    struct Seen {
        std::uint64_t frame, masterCycle;
        int thread;
    };
    std::mutex m;
    std::vector<Seen> seen;
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&, t] {
            for (int i = 0; i < kCalls; ++i) {
                // Alternate an advancing call with a write and a read.
                const Result r = (i % 3 == 0) ? s.call("core.frame_advance")
                    : (i % 3 == 1) ? s.call("core.write_memory",
                                         {{"addr", 0x0400 + t}, {"bytes", b64({static_cast<std::uint8_t>(i)})}})
                                   : s.call("core.get_cpu");
                ASSERT_FALSE(r.isError());
                std::lock_guard lock(m);
                seen.push_back({r.value["frame"], r.value["masterCycle"], t});
            }
        });
    }
    for (auto& th : threads) th.join();

    ASSERT_EQ(seen.size(), static_cast<std::size_t>(kThreads * kCalls));
    // Every frame_advance ran whole: kThreads * ceil(kCalls/3) frames.
    const Json c = ok(s, "core.counters");
    EXPECT_EQ(c["frame"], kThreads * ((kCalls + 2) / 3));
    // Sorting by masterCycle must also sort by frame (no torn pair).
    std::sort(seen.begin(), seen.end(),
        [](const Seen& a, const Seen& b) { return a.masterCycle < b.masterCycle; });
    for (std::size_t i = 1; i < seen.size(); ++i) {
        EXPECT_LE(seen[i - 1].frame, seen[i].frame);
    }
    // Each thread's last write (i = 19, the last i with i % 3 == 1) landed.
    for (int t = 0; t < kThreads; ++t) {
        EXPECT_EQ(peekByte(s, static_cast<std::uint16_t>(0x0400 + t)), 19);
    }
}

TEST(ApiConcurrency, CancelStopsARunAtAFrameBoundary)
{
    SessionConfig c = testConfig();
    c.maxRunFrames = 216000;
    Session s(c);
    ok(s, "core.load_rom", {{"path", "instr_test-v5/rom_singles/01-basics.nes"}});
    auto cancel = std::make_shared<std::atomic<bool>>(false);
    auto future = s.submit("core.run_frames", {{"frames", 216000}}, cancel);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    cancel->store(true);
    const Result r = future.get();
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(r.error->name, "Cancelled");
    const auto ran = r.error->data["framesRun"].get<std::uint64_t>();
    EXPECT_LT(ran, 216000u);
    // The session still works, and the frame counter matches the frames run.
    EXPECT_EQ(ok(s, "core.counters")["frame"], ran);
}

TEST(ApiConcurrency, ShutdownFailsQueuedCalls)
{
    auto s = std::make_unique<Session>(testConfig());
    ok(*s, "core.load_rom", {{"path", "instr_test-v5/rom_singles/01-basics.nes"}});
    auto first = s->submit("core.run_frames", {{"frames", 3600}});
    std::vector<std::future<Result>> queued;
    for (int i = 0; i < 5; ++i) {
        queued.push_back(s->submit("core.frame_advance", Json::object()));
    }
    s->shutdown();
    for (auto& f : queued) {
        const Result r = f.get();
        ASSERT_TRUE(r.isError());
        EXPECT_EQ(r.error->name, "ShuttingDown");
    }
    const Result r = first.get();
    if (r.isError()) {
        EXPECT_TRUE(r.error->name == "Cancelled" || r.error->name == "ShuttingDown") << r.error->name;
    }
    EXPECT_EQ(s->call("api.version").error->name, "ShuttingDown");
}
