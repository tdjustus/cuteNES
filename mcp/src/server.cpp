#include <cutenes/mcp/server.hpp>

#include <cutenes/api/registry.hpp>

#include <atomic>
#include <chrono>
#include <mutex>

namespace cutenes::mcp {

namespace {

// Progress notifications at most this often per request (MCP progress:
// "SHOULD implement rate limiting").
constexpr auto kProgressInterval = std::chrono::milliseconds(100);

const char* const kInstructions =
    "cuteNES, a deterministic NES emulator. Load a ROM with core.load_rom (a path in the "
    "server's library, or a blobId from the session.blob_* upload), advance it with "
    "core.run_frames or core.step_instruction, and look with core.screen, core.peek and "
    "core.read_memory. Every result carries the frame and masterCycle it was taken at. "
    "Handles (blobId, slot names) are ordinary tool arguments; the server keeps no "
    "per-client state.";

bool isRequestId(const Json& id) { return id.is_string() || id.is_number_integer(); }

// A string member, or "" (Json::value throws when the member has another type).
std::string stringAt(const Json& object, const char* key)
{
    if (!object.is_object()) return {};
    const auto it = object.find(key);
    return it != object.end() && it->is_string() ? it->get<std::string>() : std::string();
}

Json serverInfo()
{
    return {{"name", "cutenes"}, {"title", "cuteNES"}, {"version", api::surfaceVersion()}};
}

Json capabilities() { return {{"tools", {{"listChanged", true}}}}; }

// A 2026-07-28 result: resultType, and serverInfo in _meta (the schema's
// "SHOULD include this field on every response").
Json modernResult(Json result)
{
    result["resultType"] = "complete";
    result["_meta"][kMetaServerInfo] = serverInfo();
    return result;
}

Json response(const Json& id, Json result)
{
    return {{"jsonrpc", "2.0"}, {"id", id}, {"result", std::move(result)}};
}

Reply now(Json message)
{
    Reply r;
    r.kind = Reply::Kind::Now;
    r.response = std::move(message);
    return r;
}

// tools/list_changed for a 2026-07-28 subscription (with its id in _meta)
// or a 2025-11-25 connection (without).
class ListChanged final : public Subscription {
public:
    ListChanged(api::Session& session, Json id, bool legacy, Send send)
        : m_session(session), m_state(std::make_shared<State>())
    {
        m_state->id = std::move(id);
        m_state->send = std::move(send);
        Json params = Json::object();
        if (!legacy) params["_meta"][kMetaSubscription] = m_state->id;
        const Json notice = {{"jsonrpc", "2.0"}, {"method", "notifications/tools/list_changed"},
            {"params", params}};
        // The listener keeps the state alive: the session may still be
        // calling a copy of it after close().
        m_listener = session.subscribe([state = m_state, notice](const api::Event& e) {
            if (e.type == "catalog_changed" && !state->closed) {
                state->send(notice);
            }
        });
    }
    ~ListChanged() override { close(); }

    const Json& id() const override { return m_state->id; }

    void close() override
    {
        if (!m_state->closed.exchange(true)) {
            m_session.unsubscribe(m_listener);
        }
    }

    Json closingResponse() const override
    {
        Json result = modernResult(Json::object());
        result["_meta"][kMetaSubscription] = m_state->id;
        return response(m_state->id, result);
    }

private:
    struct State {
        Json id;
        Send send;
        std::atomic<bool> closed{false};
    };
    api::Session& m_session;
    std::shared_ptr<State> m_state;
    int m_listener = 0;
};

} // namespace

std::vector<std::string> supportedVersions() { return {kModernVersion, kLegacyVersion}; }

Json errorResponse(const Json& id, int code, const std::string& message, const Json& data)
{
    Json error = {{"code", code}, {"message", message}};
    if (!data.is_null()) error["data"] = data;
    Json r = {{"jsonrpc", "2.0"}, {"error", error}};
    if (!id.is_null()) r["id"] = id;
    return r;
}

std::string serialize(const Json& message)
{
    return message.dump(-1, ' ', false, Json::error_handler_t::replace);
}

Json toolResponse(const Json& id, const std::string& tool, const api::Result& result, bool modern)
{
    Json out;
    if (result.isError()) {
        const api::Error& e = *result.error;
        std::string text = e.name + ": " + e.message;
        if (!e.data.empty()) text += " " + serialize(e.data);
        out = {{"content", Json::array({{{"type", "text"}, {"text", text}}})},
            {"structuredContent", {{"name", e.name}, {"message", e.message}, {"data", e.data}}},
            {"isError", true}};
    } else {
        // The text block is the result for clients that only read text
        // (MCP tools "Structured Content"), less an image's base64, which
        // goes in its own block once.
        Json summary = result.value;
        Json content = Json::array();
        Json image;
        if (tool == "core.screen" && result.value.contains("png")) {
            summary["png"] = "(the image block)";
            image = {{"type", "image"}, {"data", result.value["png"]}, {"mimeType", "image/png"}};
        }
        content.push_back({{"type", "text"}, {"text", serialize(summary)}});
        if (!image.is_null()) content.push_back(image);
        out = {{"content", content}, {"structuredContent", result.value}};
    }
    return response(id, modern ? modernResult(out) : out);
}

struct McpServer::Impl {
    explicit Impl(api::Session& s) : session(s) {}

    Json discover() const
    {
        return modernResult({{"supportedVersions", supportedVersions()},
            {"capabilities", capabilities()}, {"instructions", kInstructions},
            {"ttlMs", kListTtlMs}, {"cacheScope", "private"}});
    }

    Json initializeResult() const
    {
        return {{"protocolVersion", kLegacyVersion}, {"capabilities", capabilities()},
            {"serverInfo", serverInfo()}, {"instructions", kInstructions}};
    }

    // One tool per command, in the registry's order (by name).
    Json toolsList(bool modern) const
    {
        Json tools = Json::array();
        for (const auto& c : session.registry().catalog()) {
            Json tool = {{"name", c["name"]}, {"description", c["description"]},
                {"inputSchema", c["inputSchema"]}, {"outputSchema", c["outputSchema"]}};
            if (const auto command = session.registry().find(c["name"])) {
                tool["annotations"] = {{"readOnlyHint", !command->mutating}};
            }
            tools.push_back(std::move(tool));
        }
        Json result = {{"tools", tools}};
        if (!modern) return result;
        result["ttlMs"] = kListTtlMs;
        result["cacheScope"] = "private";
        return modernResult(result);
    }

    Reply callTool(const Json& id, const Json& params, bool modern, const Send& send,
        const api::CancelFlag& cancel)
    {
        const std::string name = stringAt(params, "name");
        if (name.empty()) {
            return now(errorResponse(id, kInvalidParams, "tools/call needs a tool name"));
        }
        // Finding the tool is a protocol error, not a tool error (MCP tools
        // "Error Handling").
        if (!session.registry().find(name)) {
            return now(errorResponse(id, kInvalidParams, "Unknown tool: " + name));
        }
        const Json args = params.contains("arguments") && !params["arguments"].is_null()
            ? params["arguments"]
            : Json::object();
        if (!args.is_object()) {
            return now(errorResponse(id, kInvalidParams, "arguments must be an object"));
        }

        api::ProgressFn progress;
        const Json meta = params.value("_meta", Json::object());
        if (send && meta.is_object() && meta.contains("progressToken")
            && isRequestId(meta["progressToken"])) {
            struct Last {
                std::chrono::steady_clock::time_point at = std::chrono::steady_clock::now();
                double done = -1;
            };
            progress = [token = meta["progressToken"], send, cancel,
                           last = std::make_shared<Last>()](double done, double total) {
                const auto at = std::chrono::steady_clock::now();
                // Nothing more for a cancelled request; progress only grows.
                if ((cancel && cancel->load()) || at - last->at < kProgressInterval
                    || done <= last->done) {
                    return;
                }
                last->at = at;
                last->done = done;
                send({{"jsonrpc", "2.0"}, {"method", "notifications/progress"},
                    {"params", {{"progressToken", token}, {"progress", done}, {"total", total}}}});
            };
        }

        Reply r;
        r.kind = Reply::Kind::Later;
        r.result = session.submit(name, args, cancel, progress).share();
        r.respond = [id, name, modern, cancel](const api::Result& result) -> std::optional<Json> {
            if (cancel && cancel->load() && result.isError() && result.error->name == "Cancelled") {
                return std::nullopt; // the client cancelled: no response
            }
            return toolResponse(id, name, result, modern);
        };
        return r;
    }

    Reply listen(const Json& id, const Json& params, const Send& send)
    {
        if (!send) {
            return now(errorResponse(id, kInvalidRequest, "this transport cannot stream"));
        }
        const Json filter = params.value("notifications", Json::object());
        if (!filter.is_object()) {
            return now(errorResponse(id, kInvalidParams, "notifications must be an object"));
        }
        // Only what the server can honor: it has tools, not prompts or
        // resources.
        Json honored = Json::object();
        const bool tools = filter.contains("toolsListChanged") && filter["toolsListChanged"] == true;
        if (tools) honored["toolsListChanged"] = true;
        // The acknowledgement goes first (MCP subscriptions).
        send({{"jsonrpc", "2.0"}, {"method", "notifications/subscriptions/acknowledged"},
            {"params", {{"_meta", {{kMetaSubscription, id}}}, {"notifications", honored}}}});
        Reply r;
        r.kind = Reply::Kind::Subscription;
        r.subscription = std::make_shared<ListChanged>(session, id, false,
            tools ? send : Send([](const Json&) {}));
        return r;
    }

    api::Session& session;
};

McpServer::McpServer(api::Session& session) : m_impl(std::make_unique<Impl>(session)) {}

McpServer::~McpServer() = default;

api::Session& McpServer::session() { return m_impl->session; }

std::vector<std::string> McpServer::toolNames() const { return m_impl->session.registry().names(); }

std::optional<std::string> McpServer::revisionFor(const Json& message, const Connection& connection,
    Json& error) const
{
    const Json id = message.is_object() ? message.value("id", Json()) : Json();
    if (stringAt(message, "method") == "initialize") {
        return std::string(kLegacyVersion);
    }
    const Json params = message.is_object() ? message.value("params", Json::object()) : Json::object();
    const Json meta = params.is_object() ? params.value("_meta", Json::object()) : Json::object();
    if (meta.is_object() && meta.contains(kMetaVersion)) {
        const Json& v = meta[kMetaVersion];
        const std::string requested = v.is_string() ? v.get<std::string>() : serialize(v);
        for (const auto& supported : supportedVersions()) {
            if (requested == supported) return requested;
        }
        error = errorResponse(id, kUnsupportedProtocolVersion, "Unsupported protocol version",
            {{"supported", supportedVersions()}, {"requested", requested}});
        return std::nullopt;
    }
    if (!connection.legacyVersion.empty()) {
        return connection.legacyVersion;
    }
    error = errorResponse(id, kInvalidParams,
        std::string("no protocol version: send _meta \"") + kMetaVersion
            + "\" (2026-07-28) or initialize first (2025-11-25)",
        {{"supported", supportedVersions()}});
    return std::nullopt;
}

Reply McpServer::handle(const Json& message, Connection& connection, const Send& send,
    api::CancelFlag cancel)
{
    if (!message.is_object()) {
        return now(errorResponse(Json(), kInvalidRequest,
            message.is_array() ? "batches are not part of MCP" : "not a JSON-RPC message"));
    }
    const bool hasId = message.contains("id");
    const Json id = hasId ? message["id"] : Json();
    const std::string method = stringAt(message, "method");
    if (message.value("jsonrpc", Json()) != "2.0" || method.empty()
        || (hasId && !isRequestId(id))) {
        if (!hasId) return {}; // a malformed notification is ignored
        return now(errorResponse(isRequestId(id) ? id : Json(), kInvalidRequest,
            "not a JSON-RPC 2.0 request"));
    }
    if (message.contains("params") && !message["params"].is_object()) {
        if (!hasId) return {};
        return now(errorResponse(id, kInvalidParams, "params must be an object"));
    }
    if (!hasId) {
        // notifications/initialized needs nothing, and the transport handles
        // notifications/cancelled; others are ignored (JSON-RPC: no reply).
        return {};
    }

    Json error;
    const auto revision = revisionFor(message, connection, error);
    if (!revision) {
        return now(error);
    }
    const bool modern = *revision == kModernVersion;
    const Json params = message.value("params", Json::object());

    if (method == "initialize") {
        connection.legacyVersion = kLegacyVersion;
        if (connection.canNotify && send && !connection.legacyListChanged) {
            connection.legacyListChanged = std::make_shared<ListChanged>(session(), Json(), true, send);
        }
        return now(response(id, m_impl->initializeResult()));
    }
    if (method == "server/discover") {
        return now(response(id, m_impl->discover()));
    }
    if (method == "tools/list") {
        return now(response(id, m_impl->toolsList(modern)));
    }
    if (method == "tools/call") {
        return m_impl->callTool(id, params, modern, send, cancel);
    }
    if (method == "subscriptions/listen" && modern) {
        return m_impl->listen(id, params, send);
    }
    if (method == "ping" && !modern) {
        return now(response(id, Json::object()));
    }
    return now(errorResponse(id, kMethodNotFound, "Method not found: " + method));
}

} // namespace cutenes::mcp
