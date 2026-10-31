#include <cutenes/mcp/bridge.hpp>

#include <httplib.h>

#include <chrono>
#include <condition_variable>
#include <deque>
#include <fstream>
#include <future>
#include <istream>
#include <map>
#include <mutex>
#include <ostream>
#include <set>
#include <thread>

namespace cutenes::mcp {

namespace {

constexpr int kInternalError = -32603;
// The subscription a 2025-11-25 client's list changes arrive through.
const char* const kLegacyListenId = "cutenes-bridge-list-changed";

struct Target {
    std::string base; // http://host:port
    std::string path; // /mcp
};

std::optional<Target> parseUrl(const std::string& url)
{
    const std::string scheme = "http://";
    if (url.rfind(scheme, 0) != 0 || url.size() == scheme.size()) {
        return std::nullopt; // https needs TLS, which the build leaves out
    }
    const auto slash = url.find('/', scheme.size());
    if (slash == std::string::npos) return Target{url, "/"};
    return Target{url.substr(0, slash), url.substr(slash)};
}

std::string base64(const std::string& in)
{
    static const char* alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    unsigned buffer = 0;
    int bits = 0;
    for (const char ch : in) {
        buffer = (buffer << 8) | static_cast<unsigned char>(ch);
        bits += 8;
        while (bits >= 6) {
            bits -= 6;
            out.push_back(alphabet[(buffer >> bits) & 0x3F]);
        }
    }
    if (bits > 0) out.push_back(alphabet[(buffer << (6 - bits)) & 0x3F]);
    while (out.size() % 4 != 0) out.push_back('=');
    return out;
}

// A header value as 2026-07-28 "Value Encoding" asks: plain when it is
// visible ASCII without edge whitespace, otherwise the Base64 sentinel.
std::string headerValue(const std::string& value)
{
    bool plain = !value.empty() && value.front() != ' ' && value.back() != ' '
        && !(value.rfind("=?base64?", 0) == 0 && value.size() >= 11
            && value.compare(value.size() - 2, 2, "?=") == 0);
    for (const char ch : value) {
        const auto c = static_cast<unsigned char>(ch);
        plain = plain && c >= 0x20 && c <= 0x7E;
    }
    return plain ? value : "=?base64?" + base64(value) + "?=";
}

std::string stringAt(const Json& object, const char* key)
{
    if (!object.is_object()) return {};
    const auto it = object.find(key);
    return it != object.end() && it->is_string() ? it->get<std::string>() : std::string();
}

// The headers an endpoint checks against the body. `legacyVersion` is the
// version a 2025-11-25 client's initialize settled on.
httplib::Headers headersFor(const Json& message, const std::string& legacyVersion)
{
    httplib::Headers h = {{"Accept", "application/json, text/event-stream"}};
    if (!message.is_object()) return h;
    const std::string method = stringAt(message, "method");
    const Json params = message.value("params", Json::object());
    const Json meta = params.is_object() ? params.value("_meta", Json::object()) : Json::object();
    std::string version = meta.is_object() ? stringAt(meta, kMetaVersion) : "";
    if (version.empty() && method != "initialize") version = legacyVersion;
    if (!version.empty()) h.emplace("MCP-Protocol-Version", version);
    if (version == kModernVersion) {
        h.emplace("Mcp-Method", method);
        if (method == "tools/call") h.emplace("Mcp-Name", headerValue(stringAt(params, "name")));
    }
    return h;
}

std::shared_ptr<httplib::Client> makeClient(const Target& target, const std::string& token)
{
    auto c = std::make_shared<httplib::Client>(target.base);
    c->set_bearer_token_auth(token);
    c->set_connection_timeout(5, 0);
    // Runs and subscriptions last as long as they last; a subscription
    // sends a keep-alive every 15 s.
    c->set_read_timeout(24 * 3600, 0);
    return c;
}

// The data messages of a growing SSE body.
class SseReader {
public:
    template <typename F> void feed(const char* data, std::size_t n, F&& onMessage)
    {
        m_buffer.append(data, n);
        std::size_t end;
        while ((end = m_buffer.find("\n\n")) != std::string::npos) {
            const std::string block = m_buffer.substr(0, end);
            m_buffer.erase(0, end + 2);
            std::string payload;
            std::size_t start = 0;
            while (start <= block.size()) {
                auto nl = block.find('\n', start);
                if (nl == std::string::npos) nl = block.size();
                std::string line = block.substr(start, nl - start);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (line.rfind("data:", 0) == 0) {
                    line.erase(0, line.size() > 5 && line[5] == ' ' ? 6 : 5);
                    payload += (payload.empty() ? "" : "\n") + line;
                }
                start = nl + 1;
            }
            const Json message = Json::parse(payload, nullptr, false);
            if (!payload.empty() && !message.is_discarded()) onMessage(message);
        }
    }

private:
    std::string m_buffer;
};

class Writer {
public:
    explicit Writer(std::ostream& out) : m_out(out) {}
    void write(const Json& message)
    {
        std::lock_guard lock(m_mutex);
        if (m_closed) return;
        m_out << serialize(message) << '\n';
        m_out.flush();
    }
    void close()
    {
        std::lock_guard lock(m_mutex);
        m_closed = true;
    }

private:
    std::mutex m_mutex;
    std::ostream& m_out;
    bool m_closed = false;
};

// One request on the wire, so that another thread can abandon it.
struct InFlight {
    std::shared_ptr<httplib::Client> client;
    std::atomic<bool> cancelled{false};
    void cancel()
    {
        cancelled = true;
        client->stop(); // closing the connection is HTTP's cancellation
    }
};

class Bridge {
public:
    Bridge(Endpoint endpoint, Target target, std::ostream& out)
        : m_endpoint(std::move(endpoint)), m_target(std::move(target)), m_writer(out)
    {
        m_worker = std::thread([this] { work(); });
    }

    // Messages are sent in arrival order (FR-007).
    void post(std::string raw, Json message)
    {
        std::lock_guard lock(m_mutex);
        m_queue.push_back({std::move(raw), std::move(message)});
        m_cv.notify_one();
    }

    // notifications/cancelled: stdio's cancellation becomes HTTP's.
    void cancel(const std::string& key)
    {
        std::lock_guard lock(m_mutex);
        if (m_current && m_currentKey == key) {
            m_current->cancel();
        } else if (const auto it = m_streams.find(key); it != m_streams.end()) {
            it->second->cancel();
        } else {
            m_cancelledBeforeSending.insert(key);
        }
    }

    // End of input or a signal: what is running is abandoned (the endpoint
    // cancels it at a frame boundary) and nothing more is written.
    void finish()
    {
        {
            std::lock_guard lock(m_mutex);
            m_finishing = true;
            m_queue.clear();
            if (m_current) m_current->cancel();
            for (auto& [key, stream] : m_streams) stream->cancel();
            m_cv.notify_one();
        }
        m_worker.join();
        for (auto& t : m_streamThreads) t.join();
        m_writer.close();
    }

private:
    struct Queued {
        std::string raw;
        Json message;
    };

    static std::string keyOf(const Json& message)
    {
        return message.is_object() && message.contains("id") ? serialize(message["id"]) : "";
    }

    // Sends one message and hands each message that comes back to `on`.
    void exchange(const std::string& raw, const Json& message, InFlight& flight,
        const std::function<void(const Json&)>& on)
    {
        enum class Mode { Unknown, Json, Sse } mode = Mode::Unknown;
        std::string body;
        SseReader sse;
        const auto res = flight.client->Post(m_target.path, headersFor(message, m_legacyVersion), raw,
            "application/json", [&](const char* data, std::size_t n) {
                if (mode == Mode::Unknown) {
                    const std::string_view chunk(data, n);
                    const auto first = chunk.find_first_not_of(" \t\r\n");
                    if (first != std::string_view::npos) {
                        mode = chunk[first] == '{' || chunk[first] == '[' ? Mode::Json : Mode::Sse;
                    }
                }
                if (mode == Mode::Sse) {
                    sse.feed(data, n, on);
                } else {
                    body.append(data, n);
                }
                return true;
            });
        if (flight.cancelled) return; // no response to a cancelled request
        const Json id = message.is_object() && message.contains("id") ? message["id"] : Json();
        if (!res) {
            if (!id.is_null()) {
                on(errorResponse(id, kInternalError, "the MCP endpoint at " + m_endpoint.url
                    + " did not answer (" + httplib::to_string(res.error()) + ")"));
            }
            return;
        }
        if (res->status == 202 || mode == Mode::Sse) return;
        const Json reply = Json::parse(body, nullptr, false);
        if (reply.is_object() && reply.contains("jsonrpc")) {
            on(reply); // a result, or a JSON-RPC error as the endpoint sent it
        } else if (!id.is_null()) {
            on(errorResponse(id, kInternalError, res->status == 401
                    ? "the MCP endpoint refused the token (HTTP 401); the token file is from an earlier launch"
                    : "the MCP endpoint answered HTTP " + std::to_string(res->status)));
        }
    }

    void write(const Json& message) { m_writer.write(message); }

    // A long-lived request (subscriptions/listen) on its own connection.
    void stream(const std::string& key, std::string raw, Json message,
        std::function<void(const Json&)> on)
    {
        auto flight = std::make_shared<InFlight>();
        flight->client = makeClient(m_target, m_endpoint.token);
        {
            std::lock_guard lock(m_mutex);
            if (m_finishing) return;
            m_streams[key] = flight;
        }
        m_streamThreads.emplace_back([this, key, raw = std::move(raw), message = std::move(message),
                                         on = std::move(on), flight] {
            exchange(raw, message, *flight, on);
            std::lock_guard lock(m_mutex);
            m_streams.erase(key);
        });
    }

    // A 2025-11-25 client has no subscriptions/listen, and Streamable HTTP
    // has no other channel, so the bridge holds one for it and passes on
    // notifications/tools/list_changed as the 2025-11-25 notification.
    void relayLegacyListChanges()
    {
        auto acknowledged = std::make_shared<std::promise<void>>();
        auto ready = acknowledged->get_future();
        Json listen = {{"jsonrpc", "2.0"}, {"id", kLegacyListenId}, {"method", "subscriptions/listen"},
            {"params", {{"_meta", {{kMetaVersion, kModernVersion},
                                      {"io.modelcontextprotocol/clientCapabilities", Json::object()}}},
                           {"notifications", {{"toolsListChanged", true}}}}}};
        auto once = std::make_shared<std::once_flag>();
        stream(serialize(Json(kLegacyListenId)), serialize(listen), listen,
            [this, acknowledged, once](const Json& m) {
                const std::string method = stringAt(m, "method");
                if (method == "notifications/subscriptions/acknowledged") {
                    std::call_once(*once, [&] { acknowledged->set_value(); });
                } else if (method == "notifications/tools/list_changed") {
                    write({{"jsonrpc", "2.0"}, {"method", "notifications/tools/list_changed"}});
                }
            });
        // Changes after initialize must not be missed.
        ready.wait_for(std::chrono::seconds(5));
    }

    void work()
    {
        for (;;) {
            Queued q;
            {
                std::unique_lock lock(m_mutex);
                m_cv.wait(lock, [&] { return m_finishing || !m_queue.empty(); });
                if (m_finishing) return;
                q = std::move(m_queue.front());
                m_queue.pop_front();
            }
            const std::string key = keyOf(q.message);
            const std::string method = stringAt(q.message, "method");
            if (method == "subscriptions/listen" && !key.empty()) {
                stream(key, q.raw, q.message, [this](const Json& m) { write(m); });
                continue;
            }
            auto flight = std::make_shared<InFlight>();
            flight->client = makeClient(m_target, m_endpoint.token);
            {
                std::lock_guard lock(m_mutex);
                if (!key.empty() && m_cancelledBeforeSending.erase(key)) continue;
                m_current = flight;
                m_currentKey = key;
            }
            exchange(q.raw, q.message, *flight, [&](const Json& m) {
                if (method == "initialize" && m.contains("result")) {
                    m_legacyVersion = stringAt(m["result"], "protocolVersion");
                    if (!m_relayingLegacy) {
                        m_relayingLegacy = true;
                        relayLegacyListChanges();
                    }
                }
                write(m);
            });
            std::lock_guard lock(m_mutex);
            m_current.reset();
            m_currentKey.clear();
        }
    }

    Endpoint m_endpoint;
    Target m_target;
    Writer m_writer;
    std::string m_legacyVersion; // the worker's
    bool m_relayingLegacy = false;

    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::deque<Queued> m_queue;
    bool m_finishing = false;
    std::shared_ptr<InFlight> m_current;
    std::string m_currentKey;
    std::set<std::string> m_cancelledBeforeSending;
    std::map<std::string, std::shared_ptr<InFlight>> m_streams;
    std::vector<std::thread> m_streamThreads; // the worker's
    std::thread m_worker;
};

} // namespace

std::optional<Endpoint> readTokenFile(const std::filesystem::path& path, std::string& error)
{
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        error = path.string() + " does not exist";
        return std::nullopt;
    }
    std::ifstream in(path);
    const Json file = Json::parse(in, nullptr, false);
    Endpoint e{stringAt(file, "url"), stringAt(file, "token")};
    if (e.url.empty() || e.token.empty()) {
        error = path.string() + " is not a token file ({\"url\", \"token\"})";
        return std::nullopt;
    }
    return e;
}

int probe(const Endpoint& endpoint, std::string& error)
{
    const auto target = parseUrl(endpoint.url);
    if (!target) {
        error = endpoint.url + " is not an http:// URL";
        return -1;
    }
    auto client = makeClient(*target, endpoint.token);
    client->set_read_timeout(10, 0);
    const Json discover = {{"jsonrpc", "2.0"}, {"id", "cutenes-bridge-probe"},
        {"method", "server/discover"},
        {"params", {{"_meta", {{kMetaVersion, kModernVersion},
                                  {"io.modelcontextprotocol/clientCapabilities", Json::object()}}}}}};
    const auto res = client->Post(target->path, headersFor(discover, ""), serialize(discover),
        "application/json");
    if (!res) {
        error = "nothing answers at " + endpoint.url + " (" + httplib::to_string(res.error()) + ")";
        return -1;
    }
    if (res->status != 200) {
        error = endpoint.url + " answered HTTP " + std::to_string(res->status);
        return res->status;
    }
    return 0;
}

bool serveBridge(const Endpoint& endpoint, std::istream& in, std::ostream& out,
    const std::atomic<bool>& stop)
{
    const auto target = parseUrl(endpoint.url);
    if (!target) return false;

    struct Input {
        std::mutex mutex;
        std::condition_variable cv;
        std::deque<std::string> lines;
        bool eof = false;
    };
    auto input = std::make_shared<Input>();
    std::thread reader([input, &in] {
        std::string line;
        while (std::getline(in, line)) {
            std::lock_guard lock(input->mutex);
            input->lines.push_back(std::move(line));
            input->cv.notify_one();
        }
        std::lock_guard lock(input->mutex);
        input->eof = true;
        input->cv.notify_one();
    });

    Bridge bridge(endpoint, *target, out);
    bool endOfInput = false;
    while (!stop.load()) {
        std::string line;
        {
            std::unique_lock lock(input->mutex);
            input->cv.wait_for(lock, std::chrono::milliseconds(50),
                [&] { return input->eof || !input->lines.empty(); });
            if (input->lines.empty()) {
                if (input->eof) {
                    endOfInput = true;
                    break;
                }
                continue;
            }
            line = std::move(input->lines.front());
            input->lines.pop_front();
        }
        if (line.find_first_not_of(" \t\r") == std::string::npos) continue;
        if (line.back() == '\r') line.pop_back();
        Json message = Json::parse(line, nullptr, false);
        if (message.is_object() && !message.contains("id")
            && message.value("method", Json()) == "notifications/cancelled") {
            const Json params = message.value("params", Json::object());
            if (params.is_object() && params.contains("requestId")) {
                bridge.cancel(serialize(params["requestId"]));
            }
            continue;
        }
        // Unparseable text goes through as it is: the endpoint answers it
        // with the JSON-RPC error.
        bridge.post(line, message.is_discarded() ? Json() : std::move(message));
    }
    bridge.finish();
    if (endOfInput) {
        reader.join();
    } else {
        reader.detach(); // blocked on std::cin under a signal; ends with the process
    }
    return endOfInput;
}

} // namespace cutenes::mcp
