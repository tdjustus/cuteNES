// Q7 in-process (tasks A020, FR-006): the same command script on two
// sessions gives identical results.

#include "support.hpp"

using namespace cutenes::api;
using namespace cutenes::api::test;

namespace {

Json script(Session& s)
{
    Json results = Json::array();
    const auto run = [&](const std::string& name, const Json& args) {
        Json v = ok(s, name, args);
        v.erase("blobId"); // handles are per session; the bytes are compared below
        results.push_back({{"call", name}, {"result", v}});
    };
    run("core.load_rom", {{"path", "instr_test-v5/rom_singles/01-basics.nes"}});
    run("core.run_frames", {{"frames", 30}, {"script", Json::array({Json::array({"Start"}), Json::array()})}});
    run("core.write_memory", {{"addr", 0x0010}, {"bytes", b64({1, 2, 3})}});
    run("core.step_instruction", {{"count", 100}});
    run("core.trace", {{"instructions", 5}});
    run("session.save_slot", {{"name", "x"}});
    run("core.run_frames", {{"frames", 40}});
    run("session.load_slot", {{"name", "x"}});
    run("core.run_frames", {{"frames", 40}});
    run("core.peek", {{"addr", 0}, {"length", 2048}});
    run("core.read_vram", {{"addr", 0x2000}, {"length", 1024}});
    run("core.audio", {{"maxSamples", 8192}});
    run("core.screen", Json::object());
    results.push_back({{"state", b64(stateBytes(s))}});
    return results;
}

} // namespace

TEST(ApiDeterminism, TwoSessionsGiveIdenticalResults)
{
    Session a(testConfig());
    Session b(testConfig());
    const Json ra = script(a);
    const Json rb = script(b);
    ASSERT_EQ(ra.size(), rb.size());
    for (std::size_t i = 0; i < ra.size(); ++i) {
        EXPECT_EQ(ra[i], rb[i]) << "step " << i;
    }
}
