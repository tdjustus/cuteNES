// A051: the --attach bridge. Instance A serves MCP over HTTP with a GUI
// host (FakeHost) and a token file, as the Qt application will; instance B
// is `cutenes-server --attach` on stdio and finds A through the token file.
// A stdio client talking to B sees A's catalog and A's state. Expectations
// come from the transports contract (run modes, exit code 4) and the MCP
// specification (cancellation, list-changed notifications).

#include "fake_host.hpp"
#include "mcp_http_support.hpp"

#include "server.hpp"

using namespace cutenes::mcp;
using namespace cutenes::mcp::test;
using cutenes::api::test::FakeHost;

namespace {

std::filesystem::path tempTokenPath()
{
    return std::filesystem::temp_directory_path()
        / ("cutenes-attach-" + cutenes::api::token::generate().substr(0, 12)) / "mcp.token";
}

cutenes::api::SessionConfig withHost(FakeHost& host)
{
    auto c = cutenes::api::test::testConfig();
    c.host = &host;
    return c;
}

// Instance A: what the GUI runs when its MCP setting is on.
struct InstanceA {
    InstanceA() : a(withHost(host)), tokenFile(tempTokenPath())
    {
        std::string error;
        EXPECT_TRUE(cutenes::api::token::writeFile(tokenFile, a.http.url(), a.token, error)) << error;
    }
    ~InstanceA()
    {
        std::error_code ec;
        std::filesystem::remove_all(tokenFile.parent_path(), ec);
    }
    FakeHost host;
    McpHttpFixture a;
    std::filesystem::path tokenFile;
};

// Instance B, in process, for its exit code and message.
int attachInProcess(const std::vector<std::string>& args, std::string& message)
{
    const std::atomic<bool> stop{false};
    std::istringstream in;
    std::ostringstream out, err;
    const int code = cutenes::server::run(args, in, out, err, stop);
    message = err.str();
    EXPECT_TRUE(out.str().empty()) << out.str();
    return code;
}

} // namespace

TEST(McpAttach, AStdioClientSeesTheRunningInstancesCatalogAndState)
{
    InstanceA instance;
    ASSERT_TRUE(instance.a.started);
    StdioServer b({"--attach", "--mcp-token-file", instance.tokenFile.string()});
    ASSERT_TRUE(b.started);

    Json discover = b.request(modern("server/discover"));
    EXPECT_EQ(discover["result"]["supportedVersions"][0], "2026-07-28") << discover.dump();
    Json list = b.request(modern("tools/list", {}, 2));
    std::vector<std::string> tools;
    for (const auto& t : list["result"]["tools"]) tools.push_back(t["name"]);
    EXPECT_EQ(tools, instance.a.session.registry().names()); // A's catalog, gui.* included
    EXPECT_NE(std::find(tools.begin(), tools.end(), "gui.test_echo"), tools.end());

    // A's state: what A does, B's client sees, and the other way round.
    ASSERT_FALSE(instance.a.session.call("core.load_rom",
        {{"path", "instr_test-v5/rom_singles/01-basics.nes"}}).isError());
    Json info = structured(b.request(toolCall("core.cartridge_info", {}, 3)));
    EXPECT_EQ(info["cartridge"]["mapper"], 0) << info.dump(); // 01-basics is NROM
    Json ran = structured(b.request(toolCall("core.run_frames", {{"frames", 12}}, 4)));
    EXPECT_EQ(ran["framesRun"], 12);
    EXPECT_EQ(instance.a.session.call("session.status").value["frame"], ran["frame"]);
    Json echo = structured(b.request(toolCall("gui.test_echo", {{"text", "via B"}}, 5)));
    EXPECT_EQ(echo["text"], "via B");
    EXPECT_TRUE(echo["onEmulationThread"]);

    // Progress is relayed as it arrives, before the result.
    Json run = toolCall("core.run_frames", {{"frames", 600}}, 6);
    run["params"]["_meta"]["progressToken"] = "b6";
    Json result = b.request(run);
    EXPECT_EQ(structured(result)["framesRun"], 600);
    ASSERT_FALSE(b.notifications.empty());
    for (const auto& n : b.notifications) {
        EXPECT_EQ(n["method"], "notifications/progress");
        EXPECT_EQ(n["params"]["progressToken"], "b6");
    }

    // Protocol errors come back as A sent them.
    Json unknown = b.request(toolCall("core.no_such_tool", {}, 7));
    EXPECT_EQ(unknown["error"]["code"], kInvalidParams) << unknown.dump();

    b.process.closeStdin();
    EXPECT_EQ(b.process.waitExit(10s), 0);
}

TEST(McpAttach, A20251125ClientIsRelayedIncludingListChanges)
{
    InstanceA instance;
    ASSERT_TRUE(instance.a.started);
    StdioServer b({"--attach", "--mcp-token-file", instance.tokenFile.string()});
    ASSERT_TRUE(b.started);
    Json init = b.request(initialize(0));
    EXPECT_EQ(init["result"]["protocolVersion"], "2025-11-25") << init.dump();
    b.send(notification("notifications/initialized"));
    Json list = b.request(legacy("tools/list", {}, 1));
    EXPECT_FALSE(list["result"]["tools"].empty()) << list.dump();

    // A GUI change reaches the stdio client as notifications/tools/list_changed.
    Json toggled = b.request(legacy("tools/call", {{"name", "gui.test_toggle"}}, 2));
    EXPECT_TRUE(toggled["result"]["structuredContent"]["present"]) << toggled.dump();
    for (int i = 0; i < 100 && b.notifications.empty(); ++i) {
        if (auto m = b.next(50ms)) b.notifications.push_back(*m);
    }
    ASSERT_EQ(b.notifications.size(), 1u);
    EXPECT_EQ(b.notifications[0]["method"], "notifications/tools/list_changed");
}

TEST(McpAttach, A20260728SubscriptionIsRelayed)
{
    InstanceA instance;
    ASSERT_TRUE(instance.a.started);
    StdioServer b({"--attach", "--mcp-token-file", instance.tokenFile.string()});
    ASSERT_TRUE(b.started);
    b.send(modern("subscriptions/listen", {{"notifications", {{"toolsListChanged", true}}}}, "s1"));
    auto ack = b.next(10s);
    ASSERT_TRUE(ack);
    EXPECT_EQ((*ack)["method"], "notifications/subscriptions/acknowledged");
    EXPECT_EQ((*ack)["params"]["_meta"][kMetaSubscription], "s1");

    b.request(toolCall("gui.test_toggle", {}, 2));
    for (int i = 0; i < 100 && b.notifications.empty(); ++i) {
        if (auto m = b.next(50ms)) b.notifications.push_back(*m);
    }
    ASSERT_EQ(b.notifications.size(), 1u);
    EXPECT_EQ(b.notifications[0]["method"], "notifications/tools/list_changed");
    EXPECT_EQ(b.notifications[0]["params"]["_meta"][kMetaSubscription], "s1");
}

TEST(McpAttach, CancellingThroughTheBridgeStopsTheRunOnA)
{
    InstanceA instance;
    ASSERT_TRUE(instance.a.started);
    ASSERT_FALSE(instance.a.session.call("core.load_rom",
        {{"path", "instr_test-v5/rom_singles/01-basics.nes"}}).isError());
    StdioServer b({"--attach", "--mcp-token-file", instance.tokenFile.string()});
    ASSERT_TRUE(b.started);
    b.send(toolCall("core.run_frames", {{"frames", 3600}}, 5));
    std::this_thread::sleep_for(300ms);
    b.send(notification("notifications/cancelled", {{"requestId", 5}}));
    Json status = b.request(toolCall("session.status", {}, 6), 120s);
    EXPECT_TRUE(b.notifications.empty()) << b.notifications.front().dump(); // no response to 5
    EXPECT_LT(structured(status)["frame"].get<std::uint64_t>(), 3600u) << "the run was not cancelled";
}

TEST(McpAttach, NoEndpointExits4AndNamesTheSetting)
{
    std::string message;
    const auto missing = tempTokenPath();
    EXPECT_EQ(attachInProcess({"--attach", "--mcp-token-file", missing.string()}, message),
        cutenes::server::kExitNoEndpoint);
    EXPECT_NE(message.find(missing.string()), std::string::npos) << message;
    EXPECT_NE(message.find("MCP server"), std::string::npos) << message; // the GUI setting
    EXPECT_NE(message.find("--mcp-port"), std::string::npos) << message;

    // A token file left behind by an instance that is gone.
    InstanceA gone;
    const std::string url = gone.a.http.url();
    gone.a.http.stop();
    EXPECT_EQ(attachInProcess({"--attach", "--mcp-token-file", gone.tokenFile.string()}, message),
        cutenes::server::kExitNoEndpoint);
    EXPECT_NE(message.find(url), std::string::npos) << message;
}

TEST(McpAttach, AStaleTokenIsReportedAs401)
{
    InstanceA instance;
    ASSERT_TRUE(instance.a.started);
    std::string error;
    ASSERT_TRUE(cutenes::api::token::writeFile(instance.tokenFile, instance.a.http.url(),
        cutenes::api::token::generate(), error)); // a token from another launch
    std::string message;
    EXPECT_EQ(attachInProcess({"--attach", "--mcp-token-file", instance.tokenFile.string()}, message),
        cutenes::server::kExitNoEndpoint);
    EXPECT_NE(message.find("401"), std::string::npos) << message;
    EXPECT_NE(message.find(instance.tokenFile.string()), std::string::npos) << message;
}

TEST(McpAttach, TheBridgeRunsNoSessionOfItsOwn)
{
    std::string message;
    EXPECT_EQ(attachInProcess({"--attach", "--mcp-port", "0"}, message), cutenes::server::kExitUsage);
    EXPECT_EQ(attachInProcess({"--attach", "--api-port", "0"}, message), cutenes::server::kExitUsage);
    EXPECT_EQ(attachInProcess({"--attach", "--library", "x"}, message), cutenes::server::kExitUsage);
}

// The GUI makes a new token (and may get a new port) each time its MCP
// server starts: toggled off and on, or the application restarted. An MCP
// client keeps its --attach bridge running all along, so the bridge reads
// the token file again when the endpoint refuses it or is gone, and goes
// on with the new endpoint, list changes included.
TEST(McpAttach, TheBridgeFollowsTheInstanceToANewTokenAndPort)
{
    InstanceA instance;
    ASSERT_TRUE(instance.a.started);
    StdioServer b({"--attach", "--mcp-token-file", instance.tokenFile.string()});
    ASSERT_TRUE(b.started);
    Json init = b.request(initialize(0));
    EXPECT_EQ(init["result"]["protocolVersion"], "2025-11-25") << init.dump();
    b.send(notification("notifications/initialized"));
    EXPECT_TRUE(b.request(legacy("tools/call", {{"name", "session.status"}}, 1)).contains("result"));

    // The MCP server restarts: the old endpoint goes, a new one has a new
    // token and port, and the token file says so.
    instance.a.http.stop();
    McpHttpConfig config;
    config.token = cutenes::api::token::generate();
    McpHttpServer again(instance.a.mcp, config);
    std::string error;
    ASSERT_TRUE(again.start(error)) << error;
    ASSERT_NE(again.url(), instance.a.http.url());
    ASSERT_TRUE(cutenes::api::token::writeFile(instance.tokenFile, again.url(), config.token, error)) << error;

    Json status = b.request(legacy("tools/call", {{"name", "session.status"}}, 2));
    EXPECT_TRUE(status.contains("result")) << status.dump();

    // The bridge's list-changed subscription follows too, a moment later.
    for (int i = 0; i < 500 && again.activity().streams == 0; ++i) std::this_thread::sleep_for(10ms);
    ASSERT_EQ(again.activity().streams, 1) << "the subscription reconnected";
    b.notifications.clear();
    Json toggled = b.request(legacy("tools/call", {{"name", "gui.test_toggle"}}, 3));
    EXPECT_TRUE(toggled.contains("result")) << toggled.dump();
    for (int i = 0; i < 200 && b.notifications.empty(); ++i) {
        if (auto m = b.next(50ms)) b.notifications.push_back(*m);
    }
    ASSERT_FALSE(b.notifications.empty()) << "no list change after the restart";
    EXPECT_EQ(b.notifications[0]["method"], "notifications/tools/list_changed");
    again.stop();
}
