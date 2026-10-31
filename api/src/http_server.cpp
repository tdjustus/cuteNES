#include <cutenes/api/http_server.hpp>

#include <cutenes/api/jsonrpc.hpp>
#include <cutenes/api/token.hpp>

#include <httplib.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <set>
#include <thread>

namespace cutenes::api {

namespace {

constexpr std::size_t kMaxBody = 4 * 1024 * 1024;
constexpr std::size_t kMaxQueuedEvents = 256;
constexpr auto kKeepAlive = std::chrono::seconds(15);

// The host part of a Host header or URL authority: "[::1]:80" -> "::1".
std::string hostPart(const std::string& authority)
{
    if (!authority.empty() && authority.front() == '[') {
        const auto close = authority.find(']');
        return close == std::string::npos ? std::string() : authority.substr(1, close - 1);
    }
    return authority.substr(0, authority.find(':'));
}

bool loopbackOrigin(const std::string& origin)
{
    for (const char* scheme : {"http://", "https://"}) {
        const std::string prefix = scheme;
        if (origin.rfind(prefix, 0) == 0) {
            return isLoopbackHost(hostPart(origin.substr(prefix.size())));
        }
    }
    return false; // includes "null" (sandboxed pages, file: URLs)
}

// One SSE client: events wait here until its stream writes them.
struct Subscriber {
    std::mutex mutex;
    std::condition_variable cv;
    std::deque<std::string> queue;
};

std::string sseText(const Event& e)
{
    Json data = e.data;
    data["frame"] = e.frame;
    data["masterCycle"] = e.masterCycle;
    return "event: " + e.type + "\ndata: "
        + data.dump(-1, ' ', false, Json::error_handler_t::replace) + "\n\n";
}

} // namespace

bool isLoopbackHost(const std::string& host)
{
    return host == "localhost" || host == "::1" || host.rfind("127.", 0) == 0;
}

struct HttpServer::Impl {
    Impl(Session& s, HttpServerConfig c) : session(s), config(std::move(c)) {}

    Session& session;
    HttpServerConfig config;
    httplib::Server server;
    std::thread thread;
    int boundPort = 0;
    std::atomic<bool> stopping{false};

    std::mutex inflightMutex;
    std::set<CancelFlag> inflight;

    std::mutex subscriberMutex;
    std::set<std::shared_ptr<Subscriber>> subscribers;

    bool loopbackBind() const { return isLoopbackHost(config.bindAddress); }

    // 403 for a foreign Host or Origin, 401 for a missing or wrong token.
    httplib::Server::HandlerResponse guard(const httplib::Request& req, httplib::Response& res)
    {
        const auto reject = [&](int status, const char* message) {
            res.status = status;
            if (status == 401) {
                res.set_header("WWW-Authenticate", "Bearer");
            }
            res.set_content(message, "text/plain");
            return httplib::Server::HandlerResponse::Handled;
        };
        // DNS rebinding: a loopback listener answers only loopback names.
        if (loopbackBind() && !isLoopbackHost(hostPart(req.get_header_value("Host")))) {
            return reject(403, "forbidden host\n");
        }
        if (req.has_header("Origin") && !loopbackOrigin(req.get_header_value("Origin"))) {
            return reject(403, "forbidden origin\n");
        }
        const std::string expected = "Bearer " + config.token;
        if (!token::constantTimeEquals(req.get_header_value("Authorization"), expected)) {
            return reject(401, "missing or wrong token\n");
        }
        return httplib::Server::HandlerResponse::Unhandled;
    }

    Result execute(const httplib::Request& req, const std::string& method, const Json& params)
    {
        auto cancel = std::make_shared<std::atomic<bool>>(stopping.load());
        {
            std::lock_guard lock(inflightMutex);
            inflight.insert(cancel);
        }
        auto future = session.submit(method, params, cancel);
        // A client that disconnects cancels its call at the next frame
        // boundary (FR-013); nothing else in the session changes.
        while (future.wait_for(std::chrono::milliseconds(50)) != std::future_status::ready) {
            if (!cancel->load() && req.is_connection_closed()) {
                cancel->store(true);
            }
        }
        Result result = future.get();
        {
            std::lock_guard lock(inflightMutex);
            inflight.erase(cancel);
        }
        if (stopping && result.isError() && result.error->name == "Cancelled") {
            return Result::fail("ShuttingDown", "the API listener was closed");
        }
        return result;
    }

    void rpc(const httplib::Request& req, httplib::Response& res)
    {
        const auto reply = jsonrpc::handle(req.body,
            [&](const std::string& method, const Json& params) {
                return execute(req, method, params);
            });
        if (!reply) {
            res.status = 204;
            return;
        }
        res.set_content(*reply, "application/json");
    }

    void events(const httplib::Request&, httplib::Response& res)
    {
        auto sub = std::make_shared<Subscriber>();
        sub->queue.push_back(": connected\n\n");
        const int id = session.subscribe([sub](const Event& e) {
            std::lock_guard lock(sub->mutex);
            if (sub->queue.size() >= kMaxQueuedEvents) {
                sub->queue.pop_front(); // a slow reader loses the oldest
            }
            sub->queue.push_back(sseText(e));
            sub->cv.notify_one();
        });
        {
            std::lock_guard lock(subscriberMutex);
            subscribers.insert(sub);
        }
        res.set_header("Cache-Control", "no-cache");
        res.set_chunked_content_provider("text/event-stream",
            [this, sub](std::size_t, httplib::DataSink& sink) {
                std::deque<std::string> ready;
                {
                    std::unique_lock lock(sub->mutex);
                    sub->cv.wait_for(lock, kKeepAlive,
                        [&] { return stopping.load() || !sub->queue.empty(); });
                    if (stopping) {
                        return false;
                    }
                    ready.swap(sub->queue);
                }
                if (ready.empty()) {
                    ready.push_back(": keepalive\n\n");
                }
                for (const auto& text : ready) {
                    if (!sink.write(text.data(), text.size())) {
                        return false;
                    }
                }
                return true;
            },
            [this, sub, id](bool) {
                session.unsubscribe(id);
                std::lock_guard lock(subscriberMutex);
                subscribers.erase(sub);
            });
    }
};

HttpServer::HttpServer(Session& session, HttpServerConfig config)
    : m_impl(std::make_unique<Impl>(session, std::move(config)))
{
}

HttpServer::~HttpServer() { stop(); }

bool HttpServer::start(std::string& error)
{
    auto& impl = *m_impl;
    if (impl.config.token.empty()) {
        error = "no token";
        return false;
    }
    impl.server.new_task_queue = [] { return new httplib::ThreadPool(16); };
    impl.server.set_payload_max_length(kMaxBody);
    impl.server.set_pre_routing_handler(
        [&impl](const httplib::Request& req, httplib::Response& res) {
            return impl.guard(req, res);
        });
    impl.server.Post("/api/v1/rpc", [&impl](const httplib::Request& req, httplib::Response& res) {
        impl.rpc(req, res);
    });
    impl.server.Get("/api/v1/events", [&impl](const httplib::Request& req, httplib::Response& res) {
        impl.events(req, res);
    });

    if (impl.config.port == 0) {
        impl.boundPort = impl.server.bind_to_any_port(impl.config.bindAddress);
    } else {
        impl.boundPort = impl.server.bind_to_port(impl.config.bindAddress, impl.config.port)
            ? impl.config.port
            : -1;
    }
    if (impl.boundPort <= 0) {
        error = "cannot listen on " + impl.config.bindAddress + ":"
            + std::to_string(impl.config.port);
        return false;
    }
    impl.thread = std::thread([&impl] { impl.server.listen_after_bind(); });
    impl.server.wait_until_ready();
    return true;
}

void HttpServer::stop()
{
    auto& impl = *m_impl;
    if (impl.stopping.exchange(true)) {
        return;
    }
    {
        std::lock_guard lock(impl.inflightMutex);
        for (const auto& cancel : impl.inflight) {
            cancel->store(true);
        }
    }
    {
        std::lock_guard lock(impl.subscriberMutex);
        for (const auto& sub : impl.subscribers) {
            std::lock_guard subLock(sub->mutex);
            sub->cv.notify_all();
        }
    }
    impl.server.stop();
    if (impl.thread.joinable()) {
        impl.thread.join();
    }
}

int HttpServer::port() const { return m_impl->boundPort; }

std::string HttpServer::url() const
{
    const std::string& host = m_impl->config.bindAddress;
    const std::string shown = host.find(':') != std::string::npos ? "[" + host + "]" : host;
    return "http://" + shown + ":" + std::to_string(m_impl->boundPort) + "/api/v1";
}

} // namespace cutenes::api
