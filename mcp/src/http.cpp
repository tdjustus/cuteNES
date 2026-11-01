#include <cutenes/mcp/http.hpp>

#include <cutenes/api/http_server.hpp>

#include <httplib.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <set>
#include <thread>

namespace cutenes::mcp {

namespace {

constexpr std::size_t kMaxBody = 4 * 1024 * 1024;
constexpr auto kPoll = std::chrono::milliseconds(50);
constexpr auto kKeepAlive = std::chrono::seconds(15);

std::string stringAt(const Json& object, const char* key)
{
    if (!object.is_object()) return {};
    const auto it = object.find(key);
    return it != object.end() && it->is_string() ? it->get<std::string>() : std::string();
}

std::string sse(const Json& message) { return "event: message\ndata: " + serialize(message) + "\n\n"; }

// The Base64 sentinel of 2026-07-28 header values: "=?base64?...?=".
std::optional<std::string> decodeHeader(const std::string& value)
{
    const std::string prefix = "=?base64?", suffix = "?=";
    if (value.size() < prefix.size() + suffix.size() || value.rfind(prefix, 0) != 0
        || value.compare(value.size() - suffix.size(), suffix.size(), suffix) != 0) {
        return value;
    }
    static const std::string alphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    unsigned buffer = 0;
    int bits = 0;
    for (const char c : value.substr(prefix.size(), value.size() - prefix.size() - suffix.size())) {
        if (c == '=') break;
        const auto v = alphabet.find(c);
        if (v == std::string::npos) return std::nullopt;
        buffer = (buffer << 6) | static_cast<unsigned>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<char>((buffer >> bits) & 0xFF));
        }
    }
    return out;
}

// Messages a request's SSE stream has yet to write.
struct Stream {
    std::mutex mutex;
    std::condition_variable cv;
    std::deque<std::string> queue;

    void push(const Json& message)
    {
        std::lock_guard lock(mutex);
        queue.push_back(sse(message));
        cv.notify_one();
    }
};

} // namespace

struct McpHttpServer::Impl {
    Impl(McpServer& m, McpHttpConfig c) : mcp(m), config(std::move(c)) {}

    McpServer& mcp;
    McpHttpConfig config;
    httplib::Server server;
    std::thread thread;
    int boundPort = 0;
    std::atomic<bool> stopping{false};

    std::mutex inflightMutex;
    std::set<api::CancelFlag> inflight;
    std::mutex streamsMutex;
    std::set<std::shared_ptr<Stream>> streams;
    std::atomic<std::chrono::steady_clock::rep> lastCall{0}; // since the clock's epoch

    httplib::Server::HandlerResponse guard(const httplib::Request& req, httplib::Response& res)
    {
        const api::AccessDecision d = api::checkAccess(config.bindAddress,
            req.get_header_value("Host"),
            req.has_header("Origin") ? std::optional(req.get_header_value("Origin")) : std::nullopt,
            req.get_header_value("Authorization"), config.token);
        if (d.status == 0) {
            return httplib::Server::HandlerResponse::Unhandled;
        }
        res.status = d.status;
        if (d.status == 401) {
            res.set_header("WWW-Authenticate", "Bearer");
        }
        res.set_content(d.message, "text/plain");
        return httplib::Server::HandlerResponse::Handled;
    }

    static void json(httplib::Response& res, int status, const Json& message)
    {
        res.status = status;
        res.set_content(serialize(message), "application/json");
    }

    // 400 with HeaderMismatch (2026-07-28 Streamable HTTP "Server Validation").
    static void mismatch(httplib::Response& res, const Json& id, const std::string& why)
    {
        json(res, 400, errorResponse(id, kHeaderMismatch, "Header mismatch: " + why));
    }

    void track(const api::CancelFlag& cancel, bool add)
    {
        std::lock_guard lock(inflightMutex);
        if (add) {
            inflight.insert(cancel);
        } else {
            inflight.erase(cancel);
        }
    }

    // The response to a finished call; ShuttingDown when the endpoint
    // closing is what cancelled it (FR-020).
    std::optional<Json> finish(const Reply& reply, const api::Result& result, const Json& id,
        const std::string& tool, bool modern)
    {
        if (stopping && result.isError() && result.error->name == "Cancelled") {
            return toolResponse(id, tool,
                api::Result::fail("ShuttingDown", "the MCP endpoint was closed"), modern);
        }
        return reply.respond(result);
    }

    void post(const httplib::Request& req, httplib::Response& res)
    {
        lastCall = std::chrono::steady_clock::now().time_since_epoch().count();
        const Json message = Json::parse(req.body, nullptr, false);
        if (message.is_discarded()) {
            return json(res, 400, errorResponse(Json(), kParseError, "Parse error"));
        }
        if (!message.is_object()) {
            return json(res, 400, errorResponse(Json(), kInvalidRequest,
                message.is_array() ? "batches are not part of MCP" : "not a JSON-RPC message"));
        }
        const Json id = message.contains("id") ? message["id"] : Json();
        const std::string method = stringAt(message, "method");
        const Json params = message.value("params", Json::object());
        const Json meta = params.is_object() ? params.value("_meta", Json::object()) : Json::object();
        const bool hasMetaVersion = meta.is_object() && meta.contains(kMetaVersion);
        const std::string headerVersion = req.get_header_value("MCP-Protocol-Version");

        // A 2025-11-25 client sends the version as a header after
        // initialize, and nothing in _meta.
        Connection connection;
        if (method != "initialize") {
            if (headerVersion.empty()) {
                return mismatch(res, id, "missing MCP-Protocol-Version");
            }
            if (hasMetaVersion && (!meta[kMetaVersion].is_string() || meta[kMetaVersion] != headerVersion)) {
                return mismatch(res, id, "MCP-Protocol-Version does not match _meta");
            }
            if (!hasMetaVersion && headerVersion == kLegacyVersion) {
                connection.legacyVersion = kLegacyVersion;
            }
        }
        Json error;
        const auto revision = mcp.revisionFor(message, connection, error);
        const auto known = supportedVersions();
        if (!revision && !hasMetaVersion && !headerVersion.empty()
            && std::find(known.begin(), known.end(), headerVersion) == known.end()) {
            // An unsupported version named only by the header.
            return json(res, 400, errorResponse(id, kUnsupportedProtocolVersion,
                "Unsupported protocol version",
                {{"supported", supportedVersions()}, {"requested", headerVersion}}));
        }
        if (!revision) {
            return json(res, 400, error);
        }
        const bool modern = *revision == kModernVersion;
        if (modern) {
            if (req.get_header_value("Mcp-Method") != method) {
                return mismatch(res, id, "Mcp-Method does not match the method");
            }
            if (method == "tools/call") {
                const auto name = decodeHeader(req.get_header_value("Mcp-Name"));
                if (!req.has_header("Mcp-Name") || !name || *name != stringAt(params, "name")) {
                    return mismatch(res, id, "Mcp-Name does not match params.name");
                }
            }
        }
        if (!message.contains("id")) {
            // A notification: accepted, nothing owed. Cancellation over
            // HTTP is closing the stream, so notifications/cancelled has no
            // effect here.
            res.status = 202;
            return;
        }

        auto cancel = std::make_shared<std::atomic<bool>>(stopping.load());
        const bool wantsProgress = method == "tools/call" && meta.is_object() && meta.contains("progressToken");
        if (method == "subscriptions/listen" || wantsProgress) {
            return stream(message, connection, cancel, res, modern);
        }
        Reply reply = mcp.handle(message, connection, nullptr, cancel);
        if (reply.kind == Reply::Kind::Now) {
            const int code = reply.response.contains("error")
                    && reply.response["error"].value("code", 0) == kMethodNotFound
                ? 404
                : 200;
            return json(res, code, reply.response);
        }
        if (reply.kind != Reply::Kind::Later) {
            return json(res, 500, errorResponse(id, -32603, "no response"));
        }
        track(cancel, true);
        // A client that disconnects cancels its call at the next frame
        // boundary (FR-013, MCP cancellation over HTTP).
        while (reply.result.wait_for(kPoll) != std::future_status::ready) {
            if (!cancel->load() && req.is_connection_closed()) {
                cancel->store(true);
            }
        }
        track(cancel, false);
        const auto out = finish(reply, reply.result.get(), id, stringAt(params, "name"), modern);
        if (out) {
            json(res, 200, *out);
        }
    }

    // An SSE response: a run's progress then its result, or a
    // subscription's notifications until either side ends it.
    void stream(const Json& message, Connection& connection, const api::CancelFlag& cancel,
        httplib::Response& res, bool modern)
    {
        auto events = std::make_shared<Stream>();
        Reply reply = mcp.handle(message, connection,
            [events](const Json& m) { events->push(m); }, cancel);
        if (reply.kind == Reply::Kind::Now) {
            const int code = reply.response.contains("error")
                    && reply.response["error"].value("code", 0) == kMethodNotFound
                ? 404
                : 200;
            return json(res, code, reply.response);
        }
        {
            std::lock_guard lock(streamsMutex);
            streams.insert(events);
        }
        res.set_header("Cache-Control", "no-cache");
        res.set_header("X-Accel-Buffering", "no");
        const Json id = message["id"];
        const std::string tool = stringAt(message.value("params", Json::object()), "name");
        if (reply.kind == Reply::Kind::Later) {
            track(cancel, true);
        }
        auto shared = std::make_shared<Reply>(std::move(reply));
        res.set_chunked_content_provider("text/event-stream",
            [this, events, shared, cancel, id, tool, modern](std::size_t, httplib::DataSink& sink) {
                const auto write = [&](std::deque<std::string>& ready) {
                    for (const auto& text : ready) {
                        if (!sink.write(text.data(), text.size())) return false;
                    }
                    ready.clear();
                    return true;
                };
                const bool subscription = shared->kind == Reply::Kind::Subscription;
                std::deque<std::string> ready;
                {
                    std::unique_lock lock(events->mutex);
                    events->cv.wait_for(lock, subscription ? std::chrono::milliseconds(kKeepAlive) : kPoll,
                        [&] { return stopping.load() || !events->queue.empty(); });
                    ready.swap(events->queue);
                }
                if (subscription) {
                    if (stopping) {
                        // Server shutdown ends the stream gracefully (MCP
                        // subscriptions "Graceful Closure").
                        ready.push_back(sse(shared->subscription->closingResponse()));
                        write(ready);
                        sink.done();
                        return true;
                    }
                    if (ready.empty()) ready.push_back(":\n\n"); // keep-alive comment
                    return write(ready);
                }
                if (!write(ready) || (sink.is_writable && !sink.is_writable())) {
                    cancel->store(true); // the client left: cancellation
                    return false;
                }
                if (shared->result.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
                    return true;
                }
                {
                    // Progress sent before the result finished.
                    std::lock_guard lock(events->mutex);
                    ready.swap(events->queue);
                }
                const auto out = finish(*shared, shared->result.get(), id, tool, modern);
                if (out) ready.push_back(sse(*out));
                write(ready);
                sink.done();
                return true;
            },
            [this, events, shared, cancel](bool) {
                if (shared->subscription) shared->subscription->close();
                if (shared->kind == Reply::Kind::Later) {
                    cancel->store(true); // harmless once finished
                    shared->result.wait();
                    track(cancel, false);
                }
                std::lock_guard lock(streamsMutex);
                streams.erase(events);
            });
    }
};

McpHttpServer::McpHttpServer(McpServer& server, McpHttpConfig config)
    : m_impl(std::make_unique<Impl>(server, std::move(config)))
{
}

McpHttpServer::~McpHttpServer() { stop(); }

bool McpHttpServer::start(std::string& error)
{
    auto& impl = *m_impl;
    if (impl.config.token.empty()) {
        error = "no token";
        return false;
    }
    impl.server.new_task_queue = [] { return new httplib::ThreadPool(16); };
    impl.server.set_payload_max_length(kMaxBody);
    impl.server.set_pre_routing_handler(
        [&impl](const httplib::Request& req, httplib::Response& res) { return impl.guard(req, res); });
    impl.server.Post("/mcp", [&impl](const httplib::Request& req, httplib::Response& res) {
        impl.post(req, res);
    });
    // 2026-07-28 has no GET stream and no sessions to DELETE.
    const auto notAllowed = [](const httplib::Request&, httplib::Response& res) {
        res.status = 405;
        res.set_header("Allow", "POST");
    };
    impl.server.Get("/mcp", notAllowed);
    impl.server.Delete("/mcp", notAllowed);

    if (impl.config.port == 0) {
        impl.boundPort = impl.server.bind_to_any_port(impl.config.bindAddress);
    } else {
        impl.boundPort = impl.server.bind_to_port(impl.config.bindAddress, impl.config.port)
            ? impl.config.port
            : -1;
    }
    if (impl.boundPort <= 0) {
        error = "cannot listen on " + impl.config.bindAddress + ":" + std::to_string(impl.config.port);
        return false;
    }
    impl.thread = std::thread([&impl] { impl.server.listen_after_bind(); });
    impl.server.wait_until_ready();
    return true;
}

void McpHttpServer::stop()
{
    auto& impl = *m_impl;
    if (impl.stopping.exchange(true)) {
        return;
    }
    {
        std::lock_guard lock(impl.inflightMutex);
        for (const auto& cancel : impl.inflight) cancel->store(true);
    }
    {
        std::lock_guard lock(impl.streamsMutex);
        for (const auto& s : impl.streams) {
            std::lock_guard streamLock(s->mutex);
            s->cv.notify_all();
        }
    }
    impl.server.stop();
    if (impl.thread.joinable()) {
        impl.thread.join();
    }
}

int McpHttpServer::port() const { return m_impl->boundPort; }

std::string McpHttpServer::url() const
{
    const std::string& host = m_impl->config.bindAddress;
    const std::string shown = host.find(':') != std::string::npos ? "[" + host + "]" : host;
    return "http://" + shown + ":" + std::to_string(m_impl->boundPort) + "/mcp";
}

api::ListenerActivity McpHttpServer::activity() const
{
    auto& impl = *m_impl;
    api::ListenerActivity a;
    {
        std::lock_guard lock(impl.streamsMutex);
        a.streams = static_cast<int>(impl.streams.size());
    }
    {
        std::lock_guard lock(impl.inflightMutex);
        a.calls = static_cast<int>(impl.inflight.size());
    }
    a.lastCall = std::chrono::steady_clock::time_point(std::chrono::steady_clock::duration(impl.lastCall.load()));
    return a;
}

} // namespace cutenes::mcp
