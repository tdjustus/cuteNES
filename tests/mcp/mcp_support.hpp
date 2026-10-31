#pragma once

// Helpers shared by the MCP tests: requests in both revisions, an
// in-process server, cutenes-server as a stdio child process, and MCP's
// pinned schema.json files.

#include "support.hpp"

#include "json_schema.hpp"
#include "process.hpp"

#include <cutenes/mcp/server.hpp>

#include <gtest/gtest.h>

#include <fstream>
#include <map>
#include <mutex>

namespace cutenes::mcp::test {

using api::test::romDir;
using api::test::sourceDir;
using namespace std::chrono_literals;

// The _meta every 2026-07-28 request carries.
inline Json meta(const std::string& version = kModernVersion)
{
    return {{kMetaVersion, version},
        {"io.modelcontextprotocol/clientCapabilities", Json::object()},
        {"io.modelcontextprotocol/clientInfo", {{"name", "cutenes-tests"}, {"version", "1"}}}};
}

// A 2026-07-28 request: the version rides in _meta.
inline Json modern(const std::string& method, Json params = Json::object(), Json id = 1,
    const std::string& version = kModernVersion)
{
    if (params.is_null()) params = Json::object(); // a {} argument is null in nlohmann/json
    params["_meta"] = meta(version);
    return {{"jsonrpc", "2.0"}, {"id", id}, {"method", method}, {"params", params}};
}

// A 2025-11-25 request: no version of its own, `initialize` set it.
inline Json legacy(const std::string& method, Json params = Json::object(), Json id = 1)
{
    if (params.is_null()) params = Json::object();
    return {{"jsonrpc", "2.0"}, {"id", id}, {"method", method}, {"params", params}};
}

inline Json initialize(Json id = 0)
{
    return legacy("initialize",
        {{"protocolVersion", kLegacyVersion}, {"capabilities", Json::object()},
            {"clientInfo", {{"name", "cutenes-tests"}, {"version", "1"}}}},
        id);
}

inline Json toolCall(const std::string& tool, const Json& args, Json id = 1)
{
    return modern("tools/call",
        {{"name", tool}, {"arguments", args.is_null() ? Json::object() : args}}, id);
}

inline Json notification(const std::string& method, const Json& params = Json::object())
{
    return {{"jsonrpc", "2.0"}, {"method", method}, {"params", params}};
}

// The structured result of a successful tool call.
inline Json structured(const Json& response)
{
    EXPECT_TRUE(response.contains("result")) << response.dump();
    const Json result = response.value("result", Json::object());
    EXPECT_FALSE(result.value("isError", false)) << result.dump();
    return result.value("structuredContent", Json::object());
}

// MCP's published schema.json for a revision (third_party/mcp-schema).
inline const SchemaValidator& schemaFor(const std::string& version)
{
    static std::mutex m;
    static std::map<std::string, SchemaValidator> cache;
    std::lock_guard lock(m);
    auto it = cache.find(version);
    if (it == cache.end()) {
        std::ifstream in(sourceDir() + "/third_party/mcp-schema/" + version + "/schema.json");
        it = cache.emplace(version, SchemaValidator(Json::parse(in))).first;
    }
    return it->second;
}

#define EXPECT_VALID(version, instance, definition)                                            \
    do {                                                                                       \
        const auto failure_ = ::cutenes::mcp::test::schemaFor(version).validate(instance, definition); \
        EXPECT_FALSE(failure_) << (definition) << " (" << (version) << "): " << failure_.value_or("") \
                               << "\n  " << (instance).dump().substr(0, 600);                  \
    } while (0)

// A Session and McpServer in this process. Notifications land in `sent`.
struct InProcess {
    explicit InProcess(api::SessionConfig config = api::test::testConfig())
        : session(config), server(session)
    {
    }

    // The response, waiting for a tool call to finish. A subscription is
    // kept in `subscriptions` and returns null.
    Json call(const Json& message, api::CancelFlag cancel = nullptr)
    {
        if (!cancel) cancel = std::make_shared<std::atomic<bool>>(false);
        Reply reply = server.handle(message, connection,
            [this](const Json& m) {
                std::lock_guard lock(mutex);
                sent.push_back(m);
            },
            cancel);
        switch (reply.kind) {
        case Reply::Kind::Now:
            return reply.response;
        case Reply::Kind::Later:
            return reply.wait().value_or(Json());
        case Reply::Kind::Subscription:
            subscriptions.push_back(reply.subscription);
            return Json();
        case Reply::Kind::None:
            break;
        }
        return Json();
    }

    std::vector<Json> notifications()
    {
        std::lock_guard lock(mutex);
        return sent;
    }

    api::Session session;
    McpServer server;
    Connection connection;
    std::mutex mutex;
    std::vector<Json> sent;
    std::vector<std::shared_ptr<Subscription>> subscriptions;
};

// cutenes-server launched the way an MCP client launches it: stdio.
class StdioServer {
public:
    explicit StdioServer(std::vector<std::string> args = {"--library", romDir()})
    {
        started = process.start(CUTENES_SERVER_EXE, args);
        EXPECT_TRUE(started) << "cannot start " << CUTENES_SERVER_EXE;
    }

    void send(const Json& message) { EXPECT_TRUE(process.writeLine(message.dump())); }

    // The next stdout message. Every line must be one JSON-RPC 2.0 message:
    // stdout carries nothing but protocol.
    std::optional<Json> next(std::chrono::milliseconds timeout = 60s)
    {
        const auto line = process.readLine(timeout);
        if (!line) return std::nullopt;
        Json j = Json::parse(*line, nullptr, false);
        EXPECT_FALSE(j.is_discarded()) << "stdout line is not JSON: " << *line;
        EXPECT_TRUE(j.is_object() && j.value("jsonrpc", "") == "2.0") << *line;
        return j;
    }

    // Sends a request and returns its response; notifications that arrive
    // first are kept in `notifications`.
    Json request(const Json& message, std::chrono::milliseconds timeout = 60s)
    {
        send(message);
        for (;;) {
            auto m = next(timeout);
            if (!m) {
                ADD_FAILURE() << "no response to " << message.dump().substr(0, 200);
                return Json();
            }
            if (m->contains("id") && !m->contains("method") && (*m)["id"] == message["id"]) {
                return *m;
            }
            notifications.push_back(*m);
        }
    }

    ChildProcess process;
    bool started = false;
    std::vector<Json> notifications;
};

} // namespace cutenes::mcp::test
