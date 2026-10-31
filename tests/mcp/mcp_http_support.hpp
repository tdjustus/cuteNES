#pragma once

// An MCP Streamable HTTP endpoint on a free loopback port, and a client
// that sends the headers 2026-07-28 requires.

#include "mcp_support.hpp"

#include <cutenes/api/token.hpp>
#include <cutenes/mcp/http.hpp>

#include <httplib.h>

namespace cutenes::mcp::test {

struct McpHttpFixture {
    explicit McpHttpFixture(api::SessionConfig config = api::test::testConfig())
        : session(config), mcp(session), http(mcp, httpConfig()), client("127.0.0.1", 0)
    {
        std::string error;
        started = http.start(error);
        EXPECT_TRUE(started) << error;
        client = makeClient();
    }

    McpHttpConfig httpConfig()
    {
        McpHttpConfig c;
        c.token = token;
        return c;
    }

    httplib::Client makeClient()
    {
        httplib::Client c("127.0.0.1", http.port());
        c.set_read_timeout(60, 0);
        c.set_bearer_token_auth(token);
        return c;
    }

    // The headers a 2026-07-28 client derives from the body.
    static httplib::Headers standardHeaders(const Json& message)
    {
        httplib::Headers h = {{"Accept", "application/json, text/event-stream"}};
        const Json params = message.value("params", Json::object());
        const Json m = params.value("_meta", Json::object());
        if (m.contains(kMetaVersion)) {
            h.emplace("MCP-Protocol-Version", m[kMetaVersion].get<std::string>());
        }
        if (message.contains("method")) {
            h.emplace("Mcp-Method", message["method"].get<std::string>());
        }
        if (message.value("method", "") == "tools/call") {
            h.emplace("Mcp-Name", params.value("name", ""));
        }
        return h;
    }

    httplib::Result post(const Json& message, const httplib::Headers& headers)
    {
        return client.Post("/mcp", headers, message.dump(), "application/json");
    }
    httplib::Result post(const Json& message) { return post(message, standardHeaders(message)); }

    // A request that must get a 200 JSON response; returns it.
    Json rpc(const Json& message)
    {
        const auto res = post(message);
        EXPECT_TRUE(res) << "no response to " << message.dump().substr(0, 200);
        if (!res) return Json();
        EXPECT_EQ(res->status, 200) << res->body;
        return Json::parse(res->body, nullptr, false);
    }

    std::string token = api::token::generate();
    api::Session session;
    McpServer mcp;
    McpHttpServer http;
    httplib::Client client;
    bool started = false;
};

// The JSON-RPC messages in an SSE body, in order.
inline std::vector<Json> sseMessages(const std::string& body)
{
    std::vector<Json> out;
    std::size_t pos = 0;
    while ((pos = body.find("data: ", pos)) != std::string::npos) {
        const auto end = body.find('\n', pos);
        out.push_back(Json::parse(body.substr(pos + 6, end - pos - 6), nullptr, false));
        pos = end;
    }
    return out;
}

} // namespace cutenes::mcp::test
