#pragma once

// An API server on a free loopback port, and a client for it.

#include "support.hpp"

#include <cutenes/api/http_server.hpp>
#include <cutenes/api/token.hpp>

#include <httplib.h>

namespace cutenes::api::test {

struct HttpFixture {
    explicit HttpFixture(SessionConfig config = testConfig())
        : session(config), server(session, serverConfig()), client("127.0.0.1", 0)
    {
        std::string error;
        started = server.start(error);
        EXPECT_TRUE(started) << error;
        client = httplib::Client("127.0.0.1", server.port());
        client.set_read_timeout(60, 0);
        client.set_bearer_token_auth(token);
    }

    HttpServerConfig serverConfig()
    {
        HttpServerConfig c;
        c.token = token;
        return c;
    }

    // A JSON-RPC call; returns the whole response object.
    Json rpc(const std::string& method, const Json& params = Json::object(), int id = 1)
    {
        const Json request = {{"jsonrpc", "2.0"}, {"id", id}, {"method", method}, {"params", params}};
        const auto res = client.Post("/api/v1/rpc", request.dump(), "application/json");
        EXPECT_TRUE(res) << "no response to " << method;
        if (!res) return Json();
        EXPECT_EQ(res->status, 200) << res->body;
        return Json::parse(res->body, nullptr, false);
    }

    // The result of a call that must succeed.
    Json ok(const std::string& method, const Json& params = Json::object())
    {
        const Json r = rpc(method, params);
        EXPECT_TRUE(r.contains("result")) << method << ": " << r.dump();
        return r.value("result", Json());
    }

    std::string token = token::generate();
    Session session;
    HttpServer server;
    httplib::Client client;
    bool started = false;
};

} // namespace cutenes::api::test
