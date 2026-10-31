// A041, the release gate "MCP schema validates": every kind of message the
// server sends, checked against MCP's own pinned schema.json for each
// revision it serves (third_party/mcp-schema).

#include "mcp_support.hpp"

#include "blargg.hpp"

using namespace cutenes::mcp;
using namespace cutenes::mcp::test;

namespace {

// A ROM with a picture on screen and a cartridge loaded.
void loadAndRun(InProcess& p)
{
    structured(p.call(toolCall("core.load_rom", {{"path", "instr_test-v5/rom_singles/01-basics.nes"}})));
    structured(p.call(toolCall("core.run_frames", {{"frames", 30}})));
}

} // namespace

TEST(McpSchema, TheValidatorRejectsWhatTheSchemaForbids)
{
    // Not vacuous: a result missing a required field, a wrong const and a
    // wrong type each fail.
    const auto& modernSchema = schemaFor(kModernVersion);
    EXPECT_TRUE(modernSchema.validate(Json{{"tools", Json::array()}}, "ListToolsResult"));
    EXPECT_TRUE(modernSchema.validate(
        Json{{"jsonrpc", "1.0"}, {"id", 1}, {"result", {{"resultType", "complete"}}}},
        "JSONRPCResultResponse"));
    EXPECT_TRUE(modernSchema.validate(
        Json{{"type", "image"}, {"data", 5}, {"mimeType", "image/png"}}, "ImageContent"));
    EXPECT_FALSE(modernSchema.validate(
        Json{{"type", "image"}, {"data", "AA=="}, {"mimeType", "image/png"}}, "ImageContent"));
}

TEST(McpSchema, ModernResponsesValidate)
{
    InProcess p;
    const std::string v = kModernVersion;
    Json discover = p.call(modern("server/discover"));
    EXPECT_VALID(v, discover, "DiscoverResultResponse");
    Json list = p.call(modern("tools/list"));
    EXPECT_VALID(v, list, "ListToolsResultResponse");
    for (const auto& tool : list["result"]["tools"]) {
        EXPECT_VALID(v, tool, "Tool");
    }

    // A tool error (no cartridge yet) is a result with isError.
    Json noCart = p.call(toolCall("core.run_frames", {{"frames", 1}}));
    EXPECT_VALID(v, noCart, "CallToolResultResponse");
    EXPECT_EQ(noCart.value("/result/isError"_json_pointer, false), true) << noCart.dump();
    EXPECT_EQ(noCart.value("/result/structuredContent/name"_json_pointer, ""), "NoCartridge");

    loadAndRun(p);
    Json status = p.call(toolCall("session.status", {}));
    EXPECT_VALID(v, status, "CallToolResultResponse");
    Json screen = p.call(toolCall("core.screen", {}));
    EXPECT_VALID(v, screen, "CallToolResultResponse");
    for (const auto& block : screen["result"]["content"]) {
        EXPECT_VALID(v, block, "ContentBlock");
    }

    Json unknown = p.call(toolCall("core.no_such_tool", {}));
    EXPECT_VALID(v, unknown, "JSONRPCErrorResponse");
    EXPECT_VALID(v, unknown["error"], "InvalidParamsError"); // the error object
    Json version = p.call(modern("tools/list", {}, 9, "1900-01-01"));
    EXPECT_VALID(v, version, "UnsupportedProtocolVersionError");
    Json method = p.call(modern("prompts/list"));
    EXPECT_VALID(v, method["error"], "MethodNotFoundError");

    for (const Json& message : {discover, list, noCart, status, screen, unknown, version, method}) {
        EXPECT_VALID(v, message, "JSONRPCMessage");
    }
}

TEST(McpSchema, ProgressAndSubscriptionMessagesValidate)
{
    InProcess p;
    const std::string v = kModernVersion;
    structured(p.call(toolCall("core.load_rom", {{"path", "instr_test-v5/rom_singles/01-basics.nes"}})));
    Json run = toolCall("core.run_frames", {{"frames", 600}}, 2);
    run["params"]["_meta"]["progressToken"] = "run-1";
    EXPECT_VALID(v, p.call(run), "CallToolResultResponse");
    const auto progress = p.notifications();
    ASSERT_FALSE(progress.empty()) << "600 frames sent no progress";
    double last = -1;
    for (const auto& n : progress) {
        EXPECT_VALID(v, n, "ProgressNotification");
        EXPECT_EQ(n["params"]["progressToken"], "run-1");
        EXPECT_GT(n["params"]["progress"].get<double>(), last); // MUST increase
        last = n["params"]["progress"].get<double>();
        EXPECT_EQ(n["params"]["total"], 600);
    }

    p.sent.clear();
    p.call(modern("subscriptions/listen",
        {{"notifications", {{"toolsListChanged", true}, {"promptsListChanged", true}}}}, 5));
    ASSERT_EQ(p.subscriptions.size(), 1u);
    const auto ack = p.notifications();
    ASSERT_EQ(ack.size(), 1u);
    EXPECT_VALID(v, ack[0], "SubscriptionsAcknowledgedNotification");
    EXPECT_EQ(ack[0]["params"]["_meta"][kMetaSubscription], 5);
    // Only what the server honors: it has no prompts.
    EXPECT_EQ(ack[0]["params"]["notifications"], Json({{"toolsListChanged", true}}));
    const Json closing = p.subscriptions[0]->closingResponse();
    EXPECT_VALID(v, closing, "SubscriptionsListenResultResponse");
    EXPECT_EQ(closing["id"], 5);
    p.subscriptions[0]->close();
}

TEST(McpSchema, LegacyResponsesValidate)
{
    InProcess p;
    const std::string v = kLegacyVersion;
    Json init = p.call(initialize(0));
    EXPECT_VALID(v, init, "JSONRPCResultResponse");
    EXPECT_VALID(v, init["result"], "InitializeResult");
    Json list = p.call(legacy("tools/list"));
    EXPECT_VALID(v, list, "JSONRPCResultResponse");
    EXPECT_VALID(v, list["result"], "ListToolsResult");
    for (const auto& tool : list["result"]["tools"]) {
        EXPECT_VALID(v, tool, "Tool");
    }
    Json noCart = p.call(legacy("tools/call",
        {{"name", "core.run_frames"}, {"arguments", {{"frames", 1}}}}));
    EXPECT_VALID(v, noCart.value("result", Json()), "CallToolResult");
    EXPECT_EQ(noCart.value("/result/isError"_json_pointer, false), true) << noCart.dump();

    structured(p.call(legacy("tools/call",
        {{"name", "core.load_rom"}, {"arguments", {{"path", "instr_test-v5/rom_singles/01-basics.nes"}}}})));
    Json run = legacy("tools/call", {{"name", "core.run_frames"}, {"arguments", {{"frames", 300}}}}, 4);
    run["params"]["_meta"] = {{"progressToken", 77}};
    Json ran = p.call(run);
    EXPECT_VALID(v, ran["result"], "CallToolResult");
    for (const auto& n : p.notifications()) {
        EXPECT_VALID(v, n, "ProgressNotification");
    }
    Json screen = p.call(legacy("tools/call", {{"name", "core.screen"}}));
    EXPECT_VALID(v, screen["result"], "CallToolResult");
    Json ping = p.call(legacy("ping", {}, 8));
    EXPECT_VALID(v, ping, "JSONRPCResultResponse");
    for (const Json& message : {init, list, noCart, ran, screen, ping}) {
        EXPECT_VALID(v, message, "JSONRPCMessage");
    }
}
