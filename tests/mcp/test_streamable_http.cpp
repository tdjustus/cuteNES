// A047: MCP Streamable HTTP at POST /mcp. Expectations come from the
// 2026-07-28 Streamable HTTP specification (status codes, the
// Mcp-Method/Mcp-Name/MCP-Protocol-Version headers, SSE responses) and from
// the transports contract (the API's token, Host and Origin checks).

#include "mcp_http_support.hpp"

#include "server.hpp"

#include <fstream>

using namespace cutenes::mcp;
using namespace cutenes::mcp::test;

namespace {

Json body(const httplib::Result& res) { return Json::parse(res->body, nullptr, false); }

httplib::Headers without(httplib::Headers h, const std::string& name)
{
    h.erase(name);
    return h;
}

httplib::Headers with(httplib::Headers h, const std::string& name, const std::string& value)
{
    h.erase(name);
    h.emplace(name, value);
    return h;
}

std::string base64(const std::string& s)
{
    return cutenes::api::test::b64(std::vector<std::uint8_t>(s.begin(), s.end()));
}

} // namespace

TEST(McpHttp, AToolCallGetsAJsonResponse)
{
    McpHttpFixture f;
    ASSERT_TRUE(f.started);
    EXPECT_EQ(f.http.url(), "http://127.0.0.1:" + std::to_string(f.http.port()) + "/mcp");
    const auto res = f.post(modern("server/discover"));
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 200);
    EXPECT_EQ(res->get_header_value("Content-Type").rfind("application/json", 0), 0u);
    EXPECT_EQ(body(res)["result"]["supportedVersions"][0], "2026-07-28");
    EXPECT_EQ(structured(f.rpc(toolCall("session.status", {})))["mode"], "headless");
    // Mcp-Name may arrive Base64-encoded, and is decoded before comparing.
    const Json call = toolCall("api.version", {});
    const auto encoded = f.post(call,
        with(McpHttpFixture::standardHeaders(call), "Mcp-Name", "=?base64?" + base64("api.version") + "?="));
    ASSERT_TRUE(encoded);
    EXPECT_EQ(encoded->status, 200) << encoded->body;
}

TEST(McpHttp, HeadersMustMatchTheBody)
{
    McpHttpFixture f;
    ASSERT_TRUE(f.started);
    const Json call = toolCall("session.status", {}, 3);
    const auto h = McpHttpFixture::standardHeaders(call);
    for (const auto& headers : {without(h, "Mcp-Method"), without(h, "Mcp-Name"),
             without(h, "MCP-Protocol-Version"), with(h, "Mcp-Method", "tools/list"),
             with(h, "Mcp-Name", "core.reset"), with(h, "MCP-Protocol-Version", "2025-11-25"),
             with(h, "Mcp-Name", "=?base64?" + base64("core.reset") + "?=")}) {
        const auto res = f.post(call, headers);
        ASSERT_TRUE(res);
        EXPECT_EQ(res->status, 400) << res->body;
        EXPECT_EQ(body(res)["error"]["code"], kHeaderMismatch) << res->body;
        EXPECT_EQ(body(res)["id"], 3);
    }
    // Header names are case-insensitive.
    httplib::Headers lower;
    for (const auto& [k, v] : h) {
        std::string name = k;
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return std::tolower(c); });
        lower.emplace(name, v);
    }
    const auto ok = f.post(call, lower);
    ASSERT_TRUE(ok);
    EXPECT_EQ(ok->status, 200) << ok->body;
}

TEST(McpHttp, StatusCodesFollowTheSpecification)
{
    McpHttpFixture f;
    ASSERT_TRUE(f.started);
    const auto version = f.post(modern("tools/list", {}, 1, "1900-01-01"));
    ASSERT_TRUE(version);
    EXPECT_EQ(version->status, 400);
    EXPECT_EQ(body(version)["error"]["code"], kUnsupportedProtocolVersion);
    EXPECT_EQ(body(version)["error"]["data"]["requested"], "1900-01-01");

    const auto method = f.post(modern("resources/list"));
    ASSERT_TRUE(method);
    EXPECT_EQ(method->status, 404);
    EXPECT_EQ(body(method)["error"]["code"], kMethodNotFound);

    Json note = notification("notifications/cancelled", {{"requestId", 99}});
    note["params"]["_meta"] = meta();
    const auto accepted = f.post(note);
    ASSERT_TRUE(accepted);
    EXPECT_EQ(accepted->status, 202);
    EXPECT_TRUE(accepted->body.empty());

    const auto batch = f.client.Post("/mcp", McpHttpFixture::standardHeaders(modern("tools/list")),
        Json::array({modern("tools/list")}).dump(), "application/json");
    ASSERT_TRUE(batch);
    EXPECT_EQ(batch->status, 400);
    EXPECT_EQ(body(batch)["error"]["code"], kInvalidRequest);
    const auto garbage = f.client.Post("/mcp", "{nope", "application/json");
    ASSERT_TRUE(garbage);
    EXPECT_EQ(garbage->status, 400);
    EXPECT_EQ(body(garbage)["error"]["code"], kParseError);

    // No GET stream and no sessions in 2026-07-28.
    const auto get = f.client.Get("/mcp");
    ASSERT_TRUE(get);
    EXPECT_EQ(get->status, 405);
    const auto del = f.client.Delete("/mcp");
    ASSERT_TRUE(del);
    EXPECT_EQ(del->status, 405);
}

TEST(McpHttp, A20251125ClientIsServed)
{
    McpHttpFixture f;
    ASSERT_TRUE(f.started);
    const auto init = f.client.Post("/mcp", {{"Accept", "application/json, text/event-stream"}},
        initialize(1).dump(), "application/json");
    ASSERT_TRUE(init);
    EXPECT_EQ(init->status, 200) << init->body;
    EXPECT_EQ(body(init)["result"]["protocolVersion"], "2025-11-25");
    EXPECT_FALSE(init->has_header("Mcp-Session-Id")); // stateless: no session to track

    const httplib::Headers legacyHeaders = {{"Accept", "application/json, text/event-stream"},
        {"MCP-Protocol-Version", "2025-11-25"}};
    const auto list = f.client.Post("/mcp", legacyHeaders, legacy("tools/list", {}, 2).dump(),
        "application/json");
    ASSERT_TRUE(list);
    EXPECT_EQ(list->status, 200) << list->body;
    EXPECT_FALSE(body(list)["result"]["tools"].empty());
    const auto initialized = f.client.Post("/mcp", legacyHeaders,
        notification("notifications/initialized").dump(), "application/json");
    ASSERT_TRUE(initialized);
    EXPECT_EQ(initialized->status, 202);
}

TEST(McpHttp, ARunWithAProgressTokenStreamsProgressThenTheResult)
{
    McpHttpFixture f;
    ASSERT_TRUE(f.started);
    structured(f.rpc(toolCall("core.load_rom", {{"path", "instr_test-v5/rom_singles/01-basics.nes"}})));
    Json run = toolCall("core.run_frames", {{"frames", 600}}, 8);
    run["params"]["_meta"]["progressToken"] = "p8";
    const auto res = f.post(run);
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 200);
    EXPECT_EQ(res->get_header_value("Content-Type").rfind("text/event-stream", 0), 0u);
    EXPECT_EQ(res->get_header_value("X-Accel-Buffering"), "no");
    const auto messages = sseMessages(res->body);
    ASSERT_GE(messages.size(), 2u) << res->body.substr(0, 400);
    for (std::size_t i = 0; i + 1 < messages.size(); ++i) {
        EXPECT_EQ(messages[i]["method"], "notifications/progress");
        EXPECT_EQ(messages[i]["params"]["progressToken"], "p8");
        EXPECT_VALID(kModernVersion, messages[i], "ProgressNotification");
    }
    EXPECT_EQ(messages.back()["id"], 8); // the response ends the stream
    EXPECT_EQ(structured(messages.back())["framesRun"], 600);
}

TEST(McpHttp, SubscriptionsListenIsAStreamThatEndsGracefully)
{
    McpHttpFixture f;
    ASSERT_TRUE(f.started);
    std::string received;
    std::thread listener([&] {
        auto c = f.makeClient();
        const Json listen = modern("subscriptions/listen", {{"notifications", {{"toolsListChanged", true}}}}, 4);
        c.Post("/mcp", McpHttpFixture::standardHeaders(listen), listen.dump(), "application/json",
            [&](const char* data, std::size_t n) {
                received.append(data, n);
                return true;
            });
    });
    for (int i = 0; i < 200 && received.find("acknowledged") == std::string::npos; ++i) {
        std::this_thread::sleep_for(10ms);
    }
    f.http.stop(); // server shutdown ends the stream with its closing response
    listener.join();
    const auto messages = sseMessages(received);
    ASSERT_EQ(messages.size(), 2u) << received;
    EXPECT_EQ(messages[0]["method"], "notifications/subscriptions/acknowledged");
    EXPECT_EQ(messages[0]["params"]["_meta"][kMetaSubscription], 4);
    EXPECT_VALID(kModernVersion, messages[1], "SubscriptionsListenResultResponse");
}

TEST(McpHttp, TheSecurityChecksAreTheApis)
{
    McpHttpFixture f;
    ASSERT_TRUE(f.started);
    const Json call = modern("server/discover");
    httplib::Client anonymous("127.0.0.1", f.http.port());
    const auto none = anonymous.Post("/mcp", McpHttpFixture::standardHeaders(call), call.dump(),
        "application/json");
    ASSERT_TRUE(none);
    EXPECT_EQ(none->status, 401);
    anonymous.set_bearer_token_auth(cutenes::api::token::generate());
    const auto wrong = anonymous.Post("/mcp", McpHttpFixture::standardHeaders(call), call.dump(),
        "application/json");
    ASSERT_TRUE(wrong);
    EXPECT_EQ(wrong->status, 401);

    const auto origin = f.post(call, with(McpHttpFixture::standardHeaders(call), "Origin", "https://evil.example"));
    ASSERT_TRUE(origin);
    EXPECT_EQ(origin->status, 403);
    const auto host = f.post(call, with(McpHttpFixture::standardHeaders(call), "Host", "evil.example"));
    ASSERT_TRUE(host);
    EXPECT_EQ(host->status, 403);
    const auto local = f.post(call, with(McpHttpFixture::standardHeaders(call), "Origin", "http://localhost:3000"));
    ASSERT_TRUE(local);
    EXPECT_EQ(local->status, 200);
}

TEST(McpHttp, StoppingTheEndpointFailsInFlightCallsAndKeepsTheSession)
{
    McpHttpFixture f;
    ASSERT_TRUE(f.started);
    structured(f.rpc(toolCall("core.load_rom", {{"path", "instr_test-v5/rom_singles/01-basics.nes"}})));
    Json response;
    std::thread caller([&] {
        auto c = f.makeClient();
        const Json run = toolCall("core.run_frames", {{"frames", 3600}}, 2);
        const auto res = c.Post("/mcp", McpHttpFixture::standardHeaders(run), run.dump(), "application/json");
        if (res) response = Json::parse(res->body, nullptr, false);
    });
    std::this_thread::sleep_for(300ms);
    f.http.stop();
    caller.join();
    if (!response.is_null()) { // the reply can lose the race with the closing socket
        EXPECT_TRUE(response["result"]["isError"]) << response.dump();
        EXPECT_EQ(response["result"]["structuredContent"]["name"], "ShuttingDown");
    }
    // FR-020: emulation continues.
    EXPECT_FALSE(f.session.call("session.status").isError());
}

TEST(McpHttp, TheServerWritesAnMcpTokenFile)
{
    const auto dir = std::filesystem::temp_directory_path() / ("cutenes-mcp-" + cutenes::api::token::generate().substr(0, 8));
    const auto path = dir / "mcp.token";
    std::atomic<bool> stop{false};
    std::istringstream in;
    std::ostringstream out, err;
    int code = -1;
    std::thread server([&] {
        code = cutenes::server::run({"--mcp-port", "0", "--mcp-token-file", path.string(), "--library", romDir()},
            in, out, err, stop);
    });
    struct Joiner {
        std::atomic<bool>& stop;
        std::thread& thread;
        ~Joiner()
        {
            stop = true;
            if (thread.joinable()) thread.join();
        }
    } joiner{stop, server};
    Json file;
    for (int i = 0; i < 400 && (file.is_null() || file.is_discarded()); ++i) {
        std::this_thread::sleep_for(25ms);
        std::ifstream f(path);
        if (f) file = Json::parse(f, nullptr, false);
    }
    ASSERT_TRUE(file.is_object()) << err.str();
    std::string why;
    EXPECT_TRUE(cutenes::api::token::isOwnerOnly(path, &why)) << why;
    const std::string url = file["url"];
    ASSERT_EQ(url.rfind("http://127.0.0.1:", 0), 0u) << url;
    EXPECT_EQ(url.substr(url.size() - 4), "/mcp");
    EXPECT_EQ(err.str().find(file["token"].get<std::string>()), std::string::npos); // never printed
    EXPECT_TRUE(out.str().empty()); // stdio is not served alongside a listener unless asked

    httplib::Client c("127.0.0.1", std::stoi(url.substr(17)));
    c.set_bearer_token_auth(file["token"].get<std::string>());
    const Json call = modern("server/discover");
    const auto res = c.Post("/mcp", McpHttpFixture::standardHeaders(call), call.dump(), "application/json");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 200);

    stop = true;
    server.join();
    EXPECT_EQ(code, cutenes::server::kExitOk);
    EXPECT_FALSE(std::filesystem::exists(path)); // removed on a clean exit
    std::filesystem::remove_all(dir);
}

// The GUI's automation indicator (003 FR-073): an attached client holds a
// subscriptions/listen stream open, and each request is a call.
TEST(McpHttp, ActivityReportsTheListenStreamAndTheLastRequest)
{
    McpHttpFixture f;
    ASSERT_TRUE(f.started);
    const cutenes::api::ListenerActivity idle = f.http.activity();
    EXPECT_EQ(idle.streams, 0);
    EXPECT_EQ(idle.lastCall, std::chrono::steady_clock::time_point{});
    const auto before = std::chrono::steady_clock::now();
    ASSERT_TRUE(f.post(modern("server/discover")));
    EXPECT_GE(f.http.activity().lastCall, before);

    std::string received;
    std::thread listener([&] {
        auto c = f.makeClient();
        const Json listen = modern("subscriptions/listen", {{"notifications", {{"toolsListChanged", true}}}}, 4);
        c.Post("/mcp", McpHttpFixture::standardHeaders(listen), listen.dump(), "application/json",
            [&](const char* data, std::size_t n) {
                received.append(data, n);
                return true;
            });
    });
    bool open = false;
    for (int i = 0; i < 500 && !open; ++i) {
        open = f.http.activity().streams == 1;
        std::this_thread::sleep_for(10ms);
    }
    EXPECT_TRUE(open);
    f.http.stop();
    listener.join();
    EXPECT_EQ(f.http.activity().streams, 0) << "a stopped endpoint has no streams";
}
