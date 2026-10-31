// Q1 (A040): discovery over stdio, as a client launches the server. The
// expectations are the transports contract's "MCP mapping" table and the
// 2026-07-28 / 2025-11-25 specifications.

#include "mcp_support.hpp"

#include <cutenes/api/registry.hpp>

using namespace cutenes::mcp;
using namespace cutenes::mcp::test;

namespace {

std::vector<std::string> headlessNames()
{
    cutenes::api::Session s;
    return s.registry().names();
}

std::vector<std::string> namesOf(const Json& tools)
{
    std::vector<std::string> names;
    for (const auto& t : tools) names.push_back(t.value("name", ""));
    return names;
}

} // namespace

TEST(McpDiscovery, ServerDiscoverAdvertisesBothRevisionsAndTheTools)
{
    StdioServer server;
    ASSERT_TRUE(server.started);
    Json r = server.request(modern("server/discover"));
    ASSERT_TRUE(r.contains("result")) << r.dump();
    const Json& d = r["result"];
    EXPECT_EQ(d["resultType"], "complete");
    EXPECT_EQ(d["supportedVersions"], Json::array({"2026-07-28", "2025-11-25"}));
    EXPECT_EQ(d["capabilities"]["tools"]["listChanged"], true);
    EXPECT_EQ(d["_meta"][kMetaServerInfo]["name"], "cutenes");
    EXPECT_EQ(d["_meta"][kMetaServerInfo]["version"], cutenes::api::surfaceVersion());
    EXPECT_EQ(d["ttlMs"], kListTtlMs);
    EXPECT_EQ(d["cacheScope"], "private");
}

TEST(McpDiscovery, ToolsListIsTheHeadlessCatalogInADeterministicOrder)
{
    StdioServer server;
    ASSERT_TRUE(server.started);
    Json first = server.request(modern("tools/list", {}, 1));
    Json second = server.request(modern("tools/list", {}, 2));
    ASSERT_TRUE(first.contains("result")) << first.dump();
    const Json& list = first["result"];
    EXPECT_EQ(list["resultType"], "complete");
    EXPECT_EQ(list["ttlMs"], 60000);
    EXPECT_EQ(list["cacheScope"], "private");
    EXPECT_FALSE(list.contains("nextCursor")); // the whole catalog, one page

    const auto names = namesOf(list["tools"]);
    EXPECT_EQ(names, headlessNames());
    EXPECT_TRUE(std::is_sorted(names.begin(), names.end()));
    for (const auto& n : names) {
        EXPECT_EQ(n.rfind("gui.", 0), std::string::npos) << n << " in a headless catalog";
    }
    for (const auto& tool : list["tools"]) {
        EXPECT_EQ(tool["inputSchema"]["type"], "object") << tool["name"];
        EXPECT_EQ(tool["outputSchema"]["type"], "object") << tool["name"];
        EXPECT_FALSE(tool.value("description", "").empty()) << tool["name"];
    }
    EXPECT_EQ(second["result"], list); // same request, same answer
}

TEST(McpDiscovery, InitializeServesA20251125ClientForTheProcess)
{
    StdioServer server;
    ASSERT_TRUE(server.started);
    Json init = server.request(initialize(0));
    ASSERT_TRUE(init.contains("result")) << init.dump();
    EXPECT_EQ(init["result"]["protocolVersion"], "2025-11-25");
    EXPECT_EQ(init["result"]["capabilities"]["tools"]["listChanged"], true);
    EXPECT_EQ(init["result"]["serverInfo"]["name"], "cutenes");
    EXPECT_EQ(init["result"]["serverInfo"]["version"], cutenes::api::surfaceVersion());
    server.send(notification("notifications/initialized"));

    // After initialize, requests need no _meta: the connection is 2025-11-25.
    Json list = server.request(legacy("tools/list", {}, 1));
    ASSERT_TRUE(list.contains("result")) << list.dump();
    EXPECT_EQ(namesOf(list["result"]["tools"]), headlessNames());
    EXPECT_FALSE(list["result"].contains("resultType")); // a 2026-07-28 field
    Json status = server.request(legacy("tools/call", {{"name", "session.status"}}, 2));
    EXPECT_EQ(status["result"]["structuredContent"]["mode"], "headless") << status.dump();
    EXPECT_TRUE(server.request(legacy("ping", {}, 3)).contains("result"));
}

TEST(McpDiscovery, AnUnsupportedVersionIsRefusedWithTheSupportedList)
{
    StdioServer server;
    ASSERT_TRUE(server.started);
    Json r = server.request(modern("tools/list", {}, 7, "1900-01-01"));
    ASSERT_TRUE(r.contains("error")) << r.dump();
    EXPECT_EQ(r["id"], 7);
    EXPECT_EQ(r["error"]["code"], kUnsupportedProtocolVersion);
    EXPECT_EQ(r["error"]["data"]["requested"], "1900-01-01");
    EXPECT_EQ(r["error"]["data"]["supported"], Json::array({"2026-07-28", "2025-11-25"}));
}

TEST(McpDiscovery, MalformedAndUnknownRequestsGetJsonRpcErrors)
{
    StdioServer server;
    ASSERT_TRUE(server.started);
    // No version anywhere: neither _meta nor a prior initialize.
    Json bare = server.request(legacy("tools/list", {}, 1));
    EXPECT_EQ(bare["error"]["code"], kInvalidParams) << bare.dump();
    EXPECT_EQ(server.request(modern("resources/list", {}, 2))["error"]["code"], kMethodNotFound);
    Json unknownTool = server.request(toolCall("core.no_such_tool", {}, 3));
    EXPECT_EQ(unknownTool["error"]["code"], kInvalidParams) << unknownTool.dump();

    server.process.writeLine("{not json");
    const auto parse = server.next();
    ASSERT_TRUE(parse);
    EXPECT_EQ((*parse)["error"]["code"], kParseError);
    EXPECT_FALSE(parse->contains("id")); // unknown, so left out (MCP's RequestId is never null)
    // Batches were removed from MCP in 2025-06-18.
    server.process.writeLine(Json::array({modern("tools/list", {}, 4)}).dump());
    const auto batch = server.next();
    ASSERT_TRUE(batch);
    EXPECT_EQ((*batch)["error"]["code"], kInvalidRequest);
}

TEST(McpDiscovery, EndOfInputIsACleanExit)
{
    StdioServer server;
    ASSERT_TRUE(server.started);
    EXPECT_TRUE(server.request(modern("server/discover")).contains("result"));
    server.process.closeStdin();
    EXPECT_EQ(server.process.waitExit(10s), 0);
    EXPECT_FALSE(server.next(100ms)); // and nothing after the last response
}
