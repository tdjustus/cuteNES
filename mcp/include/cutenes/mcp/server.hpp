#pragma once

// The MCP adapter (contracts/transports.md "MCP mapping", research R2-R3).
// It speaks MCP 2026-07-28 and serves 2025-11-25 clients that open with
// `initialize`. Every tool is a registry command, so tools/list and
// api.catalog cannot diverge (research R1, SC-008).

#include <cutenes/api/session.hpp>

#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace cutenes::mcp {

using api::Json;

inline constexpr const char* kModernVersion = "2026-07-28";
inline constexpr const char* kLegacyVersion = "2025-11-25";
// server/discover's supportedVersions, preferred first.
std::vector<std::string> supportedVersions();
// initialize's result (2025-11-25), which needs no session: the --attach
// bridge gives it while no instance is running.
Json initializeResult();

// JSON-RPC 2.0 codes, and MCP's own.
inline constexpr int kParseError = -32700;
inline constexpr int kInvalidRequest = -32600;
inline constexpr int kMethodNotFound = -32601;
inline constexpr int kInvalidParams = -32602;
inline constexpr int kHeaderMismatch = -32020;
inline constexpr int kUnsupportedProtocolVersion = -32022;

// tools/list and server/discover cache hints (2026-07-28 caching). The
// catalog only changes when a host adds or removes commands, and then a
// list-changed notification says so.
inline constexpr std::int64_t kListTtlMs = 60000;

// The _meta keys of 2026-07-28 requests.
inline constexpr const char* kMetaVersion = "io.modelcontextprotocol/protocolVersion";
inline constexpr const char* kMetaSubscription = "io.modelcontextprotocol/subscriptionId";
inline constexpr const char* kMetaServerInfo = "io.modelcontextprotocol/serverInfo";

class Subscription;

// What a connection has negotiated. A 2025-11-25 client's `initialize`
// holds for the rest of its stdio process; on HTTP, a request with
// MCP-Protocol-Version: 2025-11-25 sets it for that request. Modern
// requests carry their version in _meta and need nothing here.
struct Connection {
    std::string legacyVersion;
    // The transport can deliver messages outside a request (stdio). A
    // 2025-11-25 client then hears notifications/tools/list_changed after
    // initialize, until the Connection is destroyed.
    bool canNotify = false;
    std::shared_ptr<Subscription> legacyListChanged;
};

// Delivers a server message that is not the response: a request's
// notifications/progress, or a subscription's notifications. Called from
// any thread, including the emulation thread, so it must not block long.
using Send = std::function<void(const Json& message)>;

// A subscriptions/listen stream (2026-07-28): list-changed notifications
// until the client or the server ends it.
class Subscription {
public:
    virtual ~Subscription() = default;
    virtual const Json& id() const = 0;
    // Stops notifications. Idempotent.
    virtual void close() = 0;
    // The empty result that ends the stream gracefully (server shutdown).
    virtual Json closingResponse() const = 0;
};

// How a message is answered.
struct Reply {
    enum class Kind {
        None,         // a notification: nothing is owed
        Now,          // `response` is ready
        Later,        // a tool call, already queued on the session: wait()
        Subscription, // a stream: `subscription` (its acknowledgement was sent)
    };
    Kind kind = Kind::None;
    Json response;
    // Later: the command's result, and the response it makes. respond()
    // returns nullopt when the client cancelled (no response is sent).
    std::shared_future<api::Result> result;
    std::function<std::optional<Json>(const api::Result&)> respond;
    std::shared_ptr<Subscription> subscription;

    // Later: blocks until the call finishes.
    std::optional<Json> wait() const { return respond(result.get()); }
};

class McpServer {
public:
    explicit McpServer(api::Session& session);
    ~McpServer();

    McpServer(const McpServer&) = delete;
    McpServer& operator=(const McpServer&) = delete;

    // Serves one parsed message: a request or a notification (batches are
    // invalid in both revisions). A tool call is queued on the session
    // before this returns, so calls keep their arrival order (FR-007).
    // `send` gets the request's progress and, for subscriptions/listen, the
    // stream's notifications. Setting *cancel abandons the request: a run
    // stops at the next frame boundary and wait() returns nullopt.
    Reply handle(const Json& message, Connection& connection, const Send& send,
        api::CancelFlag cancel);

    api::Session& session();

    // The protocol revision a request is served under: kModernVersion,
    // kLegacyVersion, or nullopt with `error` set to the JSON-RPC error.
    std::optional<std::string> revisionFor(const Json& message, const Connection& connection,
        Json& error) const;

    // The registered command names in tools/list order.
    std::vector<std::string> toolNames() const;

    struct Impl;

private:
    std::unique_ptr<Impl> m_impl;
};

// {"jsonrpc":"2.0","id":id,"error":{code,message[,data]}}; a null id
// (unknown, as for a parse error) is left out, as MCP's schema requires.
Json errorResponse(const Json& id, int code, const std::string& message,
    const Json& data = Json());

// The tools/call response for a command's result: structuredContent plus
// a text block (and an image block for core.screen's PNG), or isError with
// structuredContent {name, message, data} (command-catalog.md "Errors").
Json toolResponse(const Json& id, const std::string& tool, const api::Result& result,
    bool modern);

// One line of output: compact, no embedded newlines, invalid UTF-8
// replaced rather than thrown on.
std::string serialize(const Json& message);

} // namespace cutenes::mcp
