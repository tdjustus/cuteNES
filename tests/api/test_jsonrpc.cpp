// JSON-RPC 2.0 framing (tasks A030), checked against the examples in the
// JSON-RPC 2.0 specification, section 7.

#include "support.hpp"

#include <cutenes/api/jsonrpc.hpp>

using namespace cutenes::api;

namespace {

// Echoes params for "x.echo"; anything else is an unknown command.
Result fake(const std::string& method, const Json& params)
{
    if (method == "x.echo") return Result::ok(params);
    return Result::fail("UnknownCommand", "no " + method, {{"command", method}});
}

Json run(const std::string& body)
{
    const auto r = jsonrpc::handle(body, fake);
    return r ? Json::parse(*r) : Json("nothing");
}

} // namespace

TEST(ApiJsonRpc, CallWithNamedParams)
{
    EXPECT_EQ(run(R"({"jsonrpc":"2.0","method":"x.echo","params":{"a":1},"id":3})"),
        Json::parse(R"({"jsonrpc":"2.0","result":{"a":1},"id":3})"));
}

TEST(ApiJsonRpc, NotificationGetsNoResponse)
{
    EXPECT_EQ(run(R"({"jsonrpc":"2.0","method":"x.echo","params":{}})"), "nothing");
    EXPECT_EQ(run(R"([{"jsonrpc":"2.0","method":"x.echo"},{"jsonrpc":"2.0","method":"x.echo"}])"),
        "nothing");
}

TEST(ApiJsonRpc, UnknownMethodIsMethodNotFoundWithTheName)
{
    const Json r = run(R"({"jsonrpc":"2.0","method":"foobar","id":"1"})");
    EXPECT_EQ(r["id"], "1");
    EXPECT_EQ(r["error"]["code"], -32601);
    EXPECT_EQ(r["error"]["data"]["name"], "UnknownCommand");
    EXPECT_EQ(r["error"]["data"]["command"], "foobar");
}

TEST(ApiJsonRpc, ParseErrorHasNullId)
{
    const Json r = run(R"({"jsonrpc":"2.0","method":"foobar,"params":"bar","baz])");
    EXPECT_EQ(r["error"]["code"], -32700);
    EXPECT_TRUE(r["id"].is_null());
    EXPECT_EQ(run(R"([{"jsonrpc":"2.0","method":"sum"},{"jsonrpc":"2.0","method")")["error"]["code"],
        -32700);
}

TEST(ApiJsonRpc, InvalidRequests)
{
    EXPECT_EQ(run(R"({"jsonrpc":"2.0","method":1,"params":"bar"})")["error"]["code"], -32600);
    EXPECT_EQ(run(R"({"method":"x.echo","id":1})")["error"]["code"], -32600); // no "jsonrpc"
    // An empty array is one Invalid Request, not an empty batch.
    const Json empty = run("[]");
    EXPECT_TRUE(empty.is_object());
    EXPECT_EQ(empty["error"]["code"], -32600);
    // Each invalid element gets its own error.
    const Json three = run("[1,2,3]");
    ASSERT_EQ(three.size(), 3u);
    for (const auto& e : three) {
        EXPECT_EQ(e["error"]["code"], -32600);
        EXPECT_TRUE(e["id"].is_null());
    }
}

TEST(ApiJsonRpc, PositionalParamsAreInvalidParams)
{
    EXPECT_EQ(run(R"({"jsonrpc":"2.0","method":"x.echo","params":[1,2],"id":1})")["error"]["code"],
        -32602);
}

TEST(ApiJsonRpc, MixedBatchKeepsOrderAndSkipsNotifications)
{
    const Json r = run(R"([
        {"jsonrpc":"2.0","method":"x.echo","params":{"n":1},"id":"1"},
        {"jsonrpc":"2.0","method":"x.echo","params":{"n":2}},
        {"foo":"boo"},
        {"jsonrpc":"2.0","method":"nope","id":"5"},
        {"jsonrpc":"2.0","method":"x.echo","params":{"n":9},"id":"9"}])");
    ASSERT_EQ(r.size(), 4u);
    EXPECT_EQ(r[0]["result"]["n"], 1);
    EXPECT_EQ(r[1]["error"]["code"], -32600);
    EXPECT_EQ(r[2]["error"]["code"], -32601);
    EXPECT_EQ(r[3]["id"], "9");
}

TEST(ApiJsonRpc, CommandErrorsUseTheReservedRange)
{
    EXPECT_EQ(jsonrpc::errorCode("InvalidArguments"), -32602);
    EXPECT_EQ(jsonrpc::errorCode("NotImplemented"), -32012) << "transports.md";
    for (const char* name : {"NoCartridge", "LoadError", "LoadStateError", "WriteError",
             "PathOutsideLibrary", "LimitExceeded", "UnknownHandle", "Cancelled",
             "ShuttingDown", "NotFound", "NoBattery", "NotImplemented"}) {
        const int code = jsonrpc::errorCode(name);
        // -32000..-32019: implementation-defined; MCP 2026-07-28 reserves
        // -32020..-32099 (command-catalog.md "Errors").
        EXPECT_LE(code, -32000) << name;
        EXPECT_GE(code, -32019) << name;
    }
}
