// Q11 (A044, FR-013): a client that goes away mid-run. The run stops at a
// frame boundary, nothing else in the session changes, and the session
// still serves the next client. MCP's cancellation rules: on stdio, a
// notifications/cancelled (no response follows) or the end of input; on
// HTTP, the dropped connection.

#include "mcp_http_support.hpp"

using namespace cutenes::mcp;
using namespace cutenes::mcp::test;

namespace {

constexpr const char* kRom = "instr_test-v5/rom_singles/01-basics.nes";
constexpr int kLongRun = 3600; // the default per-call cap

} // namespace

TEST(McpDisconnect, StdioCancelledStopsTheRunAndSendsNoResponse)
{
    StdioServer server;
    ASSERT_TRUE(server.started);
    ASSERT_FALSE(server.request(toolCall("core.load_rom", {{"path", kRom}}, 1))["result"].value("isError", false));
    server.send(toolCall("core.run_frames", {{"frames", kLongRun}}, 5));
    std::this_thread::sleep_for(200ms);
    server.send(notification("notifications/cancelled", {{"requestId", 5}, {"reason", "test"}}));

    // Calls run in arrival order, so a response to 5 would come before 6.
    Json status = server.request(toolCall("session.status", {}, 6), 120s);
    EXPECT_TRUE(server.notifications.empty()) << server.notifications.front().dump();
    const auto frame = structured(status)["frame"].get<std::uint64_t>();
    EXPECT_LT(frame, static_cast<std::uint64_t>(kLongRun)) << "the run was not cancelled";

    // The session is intact: the next run advances exactly as asked.
    Json after = structured(server.request(toolCall("core.run_frames", {{"frames", 10}}, 7)));
    EXPECT_EQ(after["framesRun"], 10);
    EXPECT_EQ(after["frame"].get<std::uint64_t>(), frame + 10);
}

TEST(McpDisconnect, StdioEndOfInputCancelsTheRunAndExits)
{
    StdioServer server;
    ASSERT_TRUE(server.started);
    ASSERT_FALSE(server.request(toolCall("core.load_rom", {{"path", kRom}}, 1))["result"].value("isError", false));
    server.send(toolCall("core.run_frames", {{"frames", kLongRun}}, 2));
    std::this_thread::sleep_for(200ms);
    const auto start = std::chrono::steady_clock::now();
    server.process.closeStdin(); // the client is gone
    // 3600 frames is a minute of emulated time; a cancelled run ends at the
    // next frame boundary.
    EXPECT_EQ(server.process.waitExit(5s), 0);
    EXPECT_LT(std::chrono::steady_clock::now() - start, 5s);
}

TEST(McpDisconnect, HttpClientDroppingMidRunCancelsItAndTheNextClientCarriesOn)
{
    McpHttpFixture f;
    ASSERT_TRUE(f.started);
    structured(f.rpc(toolCall("core.load_rom", {{"path", kRom}})));
    {
        auto impatient = f.makeClient();
        impatient.set_read_timeout(0, 300000); // gives up after 300 ms
        const Json run = toolCall("core.run_frames", {{"frames", kLongRun}}, 2);
        const auto res = impatient.Post("/mcp", McpHttpFixture::standardHeaders(run), run.dump(),
            "application/json");
        EXPECT_FALSE(res) << "the run finished inside 300 ms";
    }
    auto next = f.makeClient();
    const Json status = toolCall("session.status", {}, 3);
    const auto res = next.Post("/mcp", McpHttpFixture::standardHeaders(status), status.dump(),
        "application/json");
    ASSERT_TRUE(res);
    const auto frame = structured(Json::parse(res->body))["frame"].get<std::uint64_t>();
    EXPECT_LT(frame, static_cast<std::uint64_t>(kLongRun)) << "the run was not cancelled";
    Json after = structured(f.rpc(toolCall("core.run_frames", {{"frames", 10}}, 4)));
    EXPECT_EQ(after["frame"].get<std::uint64_t>(), frame + 10);
}
