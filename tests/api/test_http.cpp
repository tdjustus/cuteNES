// The API over HTTP (tasks A030): JSON-RPC on POST /api/v1/rpc, events on
// GET /api/v1/events, and Q6-Q8 repeated over the wire.

#include "http_support.hpp"

#include <future>
#include <thread>

using namespace cutenes::api;
using namespace cutenes::api::test;

namespace {

const char* const kBasics = "instr_test-v5/rom_singles/01-basics.nes";

} // namespace

TEST(ApiHttp, CallsReturnResultsAndStructuredErrors)
{
    HttpFixture f;
    const Json loaded = f.ok("core.load_rom", {{"path", kBasics}});
    EXPECT_EQ(loaded["cartridge"]["mapper"], 0);
    const Json run = f.ok("core.run_frames", {{"frames", 10}});
    EXPECT_EQ(run["frame"], 10);

    const Json bad = f.rpc("core.run_frames", {{"frames", 0}});
    EXPECT_EQ(bad["error"]["code"], -32602);
    EXPECT_EQ(bad["error"]["data"]["name"], "InvalidArguments");
    EXPECT_EQ(bad["error"]["data"]["path"], "/frames");
    EXPECT_EQ(f.rpc("core.fly")["error"]["data"]["name"], "UnknownCommand");
}

TEST(ApiHttp, BatchesRunInOrderAndNotificationsGet204)
{
    HttpFixture f;
    f.ok("core.load_rom", {{"path", kBasics}});
    const Json batch = Json::array({
        {{"jsonrpc", "2.0"}, {"id", 1}, {"method", "core.run_frames"}, {"params", {{"frames", 2}}}},
        {{"jsonrpc", "2.0"}, {"method", "core.run_frames"}, {"params", {{"frames", 3}}}},
        {{"jsonrpc", "2.0"}, {"id", 2}, {"method", "core.counters"}},
    });
    const auto res = f.client.Post("/api/v1/rpc", batch.dump(), "application/json");
    ASSERT_TRUE(res);
    const Json r = Json::parse(res->body);
    ASSERT_EQ(r.size(), 2u);
    EXPECT_EQ(r[0]["result"]["frame"], 2);
    EXPECT_EQ(r[1]["result"]["frame"], 5); // the notification ran in between

    const Json note = {{"jsonrpc", "2.0"}, {"method", "core.run_frames"}, {"params", {{"frames", 1}}}};
    const auto nr = f.client.Post("/api/v1/rpc", note.dump(), "application/json");
    ASSERT_TRUE(nr);
    EXPECT_EQ(nr->status, 204);
    EXPECT_EQ(f.ok("core.counters")["frame"], 6);

    const auto parseError = f.client.Post("/api/v1/rpc", "{oops", "application/json");
    ASSERT_TRUE(parseError);
    EXPECT_EQ(Json::parse(parseError->body)["error"]["code"], -32700);
}

TEST(ApiHttp, EventStreamReportsStateChanges)
{
    HttpFixture f;
    httplib::Client events("127.0.0.1", f.server.port());
    events.set_bearer_token_auth(f.token);
    std::promise<void> connected;
    std::promise<std::string> stateChanged;
    bool connectedSet = false;
    bool changedSet = false;
    std::string buffer;
    std::thread reader([&] {
        events.Get("/api/v1/events", [&](const char* data, std::size_t size) {
            buffer.append(data, size);
            if (!connectedSet && buffer.find(": connected") != std::string::npos) {
                connectedSet = true;
                connected.set_value();
            }
            const auto at = buffer.find("event: state_changed\ndata: ");
            if (!changedSet && at != std::string::npos && buffer.find("\n\n", at) != std::string::npos) {
                changedSet = true;
                const auto start = at + std::string("event: state_changed\ndata: ").size();
                stateChanged.set_value(buffer.substr(start, buffer.find('\n', start) - start));
                return false; // close the stream
            }
            return true;
        });
    });
    ASSERT_EQ(connected.get_future().wait_for(std::chrono::seconds(10)), std::future_status::ready);
    f.ok("core.load_rom", {{"path", kBasics}});
    auto changed = stateChanged.get_future();
    ASSERT_EQ(changed.wait_for(std::chrono::seconds(10)), std::future_status::ready);
    const Json data = Json::parse(changed.get());
    EXPECT_EQ(data["command"], "core.load_rom");
    EXPECT_EQ(data["frame"], 0);
    reader.join();
}

// Q6 over HTTP: the error leaves the state unchanged.
TEST(ApiHttp, ErrorsChangeNoState)
{
    HttpFixture f;
    f.ok("core.load_rom", {{"path", kBasics}});
    f.ok("core.run_frames", {{"frames", 5}});
    const auto before = stateBytes(f.session);
    EXPECT_EQ(f.rpc("core.load_rom", {{"path", "../CMakeLists.txt"}})["error"]["data"]["name"],
        "PathOutsideLibrary");
    EXPECT_EQ(f.rpc("core.load_state", {{"bytes", b64({1, 2, 3, 4})}})["error"]["data"]["error"],
        "BadMagic");
    EXPECT_EQ(f.rpc("core.write_cpu_register", {{"addr", 0}, {"value", 1}})["error"]["code"], -32004);
    EXPECT_EQ(stateBytes(f.session), before);
}

// Q7: the same script in process and over HTTP gives identical results.
TEST(ApiHttp, SameResultsInProcessAndOverHttp)
{
    const Json script = Json::array({
        Json::array({"core.load_rom", {{"path", kBasics}}}),
        Json::array({"core.run_frames", {{"frames", 20}, {"script", Json::array({Json::array({"A"})})}}}),
        Json::array({"core.step_instruction", {{"count", 50}}}),
        Json::array({"core.peek", {{"addr", 0}, {"length", 2048}}}),
        Json::array({"core.frame_hash", Json::object()}),
        Json::array({"core.get_cpu", Json::object()}),
    });
    Session direct(testConfig());
    HttpFixture f;
    for (const auto& step : script) {
        EXPECT_EQ(ok(direct, step[0], step[1]), f.ok(step[0], step[1])) << step[0];
    }
}

// Q8 over HTTP: several clients interleave; each result is whole.
TEST(ApiHttp, ConcurrentClientsAreSerialized)
{
    HttpFixture f;
    f.ok("core.load_rom", {{"path", kBasics}});
    constexpr int kClients = 4;
    constexpr int kCalls = 10;
    std::vector<std::thread> threads;
    std::atomic<int> failures{0};
    for (int c = 0; c < kClients; ++c) {
        threads.emplace_back([&] {
            httplib::Client client("127.0.0.1", f.server.port());
            client.set_bearer_token_auth(f.token);
            for (int i = 0; i < kCalls; ++i) {
                const Json req = {{"jsonrpc", "2.0"}, {"id", i}, {"method", "core.frame_advance"}};
                const auto res = client.Post("/api/v1/rpc", req.dump(), "application/json");
                if (!res || !Json::parse(res->body).contains("result")) ++failures;
            }
        });
    }
    for (auto& t : threads) t.join();
    EXPECT_EQ(failures.load(), 0);
    EXPECT_EQ(f.ok("core.counters")["frame"], kClients * kCalls);
}

// FR-013: a client that disconnects mid-run cancels it at a frame boundary;
// the session still serves the next client.
TEST(ApiHttp, DisconnectCancelsTheRun)
{
    SessionConfig c = testConfig();
    c.maxRunFrames = 216000;
    HttpFixture f(c);
    f.ok("core.load_rom", {{"path", kBasics}});
    {
        httplib::Client impatient("127.0.0.1", f.server.port());
        impatient.set_bearer_token_auth(f.token);
        impatient.set_read_timeout(0, 300000); // give up after 0.3 s
        const Json req = {{"jsonrpc", "2.0"}, {"id", 1}, {"method", "core.run_frames"},
            {"params", {{"frames", 216000}}}};
        const auto res = impatient.Post("/api/v1/rpc", req.dump(), "application/json");
        EXPECT_FALSE(res); // timed out and closed the connection
    }
    // The next call waits behind the cancelled run, then sees its end.
    const auto start = std::chrono::steady_clock::now();
    const Json counters = f.ok("core.counters");
    EXPECT_LT(counters["frame"].get<std::uint64_t>(), 216000u);
    EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::seconds(30));
}

// FR-020: closing the listener ends in-flight calls with ShuttingDown and
// leaves the session running.
TEST(ApiHttp, StoppingTheListenerKeepsTheSession)
{
    SessionConfig c = testConfig();
    c.maxRunFrames = 216000;
    HttpFixture f(c);
    f.ok("core.load_rom", {{"path", kBasics}});
    std::thread caller([&] {
        const Json req = {{"jsonrpc", "2.0"}, {"id", 1}, {"method", "core.run_frames"},
            {"params", {{"frames", 216000}}}};
        const auto res = f.client.Post("/api/v1/rpc", req.dump(), "application/json");
        if (res && res->status == 200) {
            EXPECT_EQ(Json::parse(res->body)["error"]["data"]["name"], "ShuttingDown");
        }
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    f.server.stop();
    caller.join();
    const Json counters = ok(f.session, "core.counters");
    EXPECT_LT(counters["frame"].get<std::uint64_t>(), 216000u);
    ok(f.session, "core.frame_advance");
}
