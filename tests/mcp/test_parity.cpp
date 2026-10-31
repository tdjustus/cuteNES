// SC-008 (A043): MCP's tools/list and the API's api.catalog are one
// catalog: the same names in the same order, with the same schemas.

#include "mcp_support.hpp"

using namespace cutenes::mcp;
using namespace cutenes::mcp::test;

TEST(McpParity, ToolsListIsApiCatalog)
{
    InProcess p;
    Json tools = p.call(modern("tools/list"))["result"]["tools"];
    Json catalog = structured(p.call(toolCall("api.catalog", {})))["commands"];
    ASSERT_EQ(tools.size(), catalog.size());
    for (std::size_t i = 0; i < tools.size(); ++i) {
        EXPECT_EQ(tools[i]["name"], catalog[i]["name"]);
        EXPECT_EQ(tools[i]["description"], catalog[i]["description"]) << tools[i]["name"];
        EXPECT_EQ(tools[i]["inputSchema"], catalog[i]["inputSchema"]) << tools[i]["name"];
        EXPECT_EQ(tools[i]["outputSchema"], catalog[i]["outputSchema"]) << tools[i]["name"];
    }
    // And over the 2025-11-25 path too.
    p.call(initialize());
    Json legacyTools = p.call(legacy("tools/list"))["result"]["tools"];
    EXPECT_EQ(legacyTools, tools);
}

TEST(McpParity, AToolCallIsTheCommandsResult)
{
    InProcess p;
    cutenes::api::Session direct(cutenes::api::test::testConfig());
    const Json args = {{"path", "instr_test-v5/rom_singles/01-basics.nes"}};
    EXPECT_EQ(structured(p.call(toolCall("core.load_rom", args))),
        cutenes::api::test::ok(direct, "core.load_rom", args));
    EXPECT_EQ(structured(p.call(toolCall("core.run_frames", {{"frames", 20}}))),
        cutenes::api::test::ok(direct, "core.run_frames", {{"frames", 20}}));

    // An error keeps the command's name, message and data.
    Json failed = p.call(toolCall("core.peek", {{"addr", 70000}, {"length", 1}}));
    const auto error = direct.call("core.peek", {{"addr", 70000}, {"length", 1}}).error;
    ASSERT_TRUE(error);
    EXPECT_TRUE(failed["result"]["isError"]);
    EXPECT_EQ(failed["result"]["structuredContent"],
        Json({{"name", error->name}, {"message", error->message}, {"data", error->data}}));
    const Json text = failed["result"]["content"][0];
    EXPECT_EQ(text["type"], "text");
    EXPECT_NE(text["text"].get<std::string>().find(error->name), std::string::npos);
}
