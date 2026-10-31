// Q12 latency (tasks A034, SC-003): 95% of non-emulating calls finish in
// under 100 ms over HTTP. Optimized builds only: Debug and sanitizer
// timings say nothing about the product.

#include "http_support.hpp"

#include <algorithm>
#include <chrono>

using namespace cutenes::api;
using namespace cutenes::api::test;

TEST(ApiLatency, NonEmulatingCallsAreFast)
{
#if !defined(NDEBUG) || defined(__SANITIZE_ADDRESS__)
    GTEST_SKIP() << "timing applies to optimized builds only";
#endif
    HttpFixture f;
    f.ok("core.load_rom", {{"path", "instr_test-v5/rom_singles/01-basics.nes"}});
    f.ok("core.run_frames", {{"frames", 10}});
    const std::pair<const char*, Json> calls[] = {
        {"session.status", Json::object()},
        {"core.peek", {{"addr", 0}, {"length", 256}}},
        {"core.get_cpu", Json::object()},
        {"api.catalog", Json::object()},
    };
    for (const auto& [method, params] : calls) {
        std::vector<double> ms;
        for (int i = 0; i < 100; ++i) {
            const auto start = std::chrono::steady_clock::now();
            f.ok(method, params);
            ms.push_back(std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - start).count());
        }
        std::sort(ms.begin(), ms.end());
        EXPECT_LT(ms[94], 100.0) << method << " p95";
    }
}
