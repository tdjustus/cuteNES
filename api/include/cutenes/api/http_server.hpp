#pragma once

// The API over HTTP (contracts/transports.md "API protocol"):
// POST /api/v1/rpc (JSON-RPC 2.0) and GET /api/v1/events (Server-Sent
// Events), behind the per-launch bearer token and the loopback Host and
// Origin checks (research R11, FR-024).

#include <cutenes/api/session.hpp>

#include <chrono>
#include <memory>
#include <optional>
#include <string>

namespace cutenes::api {

struct HttpServerConfig {
    // Loopback unless the operator asks otherwise (FR-008, --bind).
    std::string bindAddress = "127.0.0.1";
    int port = 0; // 0 picks a free port
    // Required; every request must send "Authorization: Bearer <token>".
    std::string token;
};

// What a listener is doing, for "is a client there?" (003 FR-073): open
// event streams, calls running, and when the last call came (the epoch
// before the first).
struct ListenerActivity {
    int streams = 0;
    int calls = 0;
    std::chrono::steady_clock::time_point lastCall{};
};

class HttpServer {
public:
    HttpServer(Session& session, HttpServerConfig config);
    ~HttpServer(); // stops

    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;

    // Binds and serves on background threads. On failure returns false and
    // sets `error` (the server exits 3 on a bind failure).
    bool start(std::string& error);
    // Closes the listener: in-flight and queued calls from it fail with
    // ShuttingDown and event streams end. The session keeps running (FR-020).
    void stop();

    int port() const;
    std::string url() const; // http://127.0.0.1:N/api/v1
    ListenerActivity activity() const; // any thread

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

// "127.0.0.1", "::1" and "localhost" (any 127.0.0.0/8 address too).
bool isLoopbackHost(const std::string& host);

// The checks every HTTP listener makes before routing (research R11, the
// API and MCP alike): 403 for a non-loopback Host on a loopback listener
// (DNS rebinding) or a foreign Origin, 401 for a missing or wrong bearer
// token. Status 0 lets the request through.
struct AccessDecision {
    int status = 0;
    std::string message;
};
AccessDecision checkAccess(const std::string& bindAddress, const std::string& hostHeader,
    const std::optional<std::string>& origin, const std::string& authorization,
    const std::string& token);

} // namespace cutenes::api
