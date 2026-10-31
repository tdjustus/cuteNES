#pragma once

// MCP Streamable HTTP (2026-07-28): one POST endpoint at /mcp, behind the
// same token, Host and Origin checks as the API (research R11). A request
// gets a JSON response, or an SSE stream when it asked for progress or is
// subscriptions/listen.

#include <cutenes/mcp/server.hpp>

#include <memory>
#include <string>

namespace cutenes::mcp {

struct McpHttpConfig {
    std::string bindAddress = "127.0.0.1";
    int port = 0; // 0 picks a free port
    // Required; every request must send "Authorization: Bearer <token>".
    std::string token;
};

class McpHttpServer {
public:
    McpHttpServer(McpServer& server, McpHttpConfig config);
    ~McpHttpServer(); // stops

    McpHttpServer(const McpHttpServer&) = delete;
    McpHttpServer& operator=(const McpHttpServer&) = delete;

    // Binds and serves on background threads; false (with `error`) on
    // failure.
    bool start(std::string& error);
    // Closes the endpoint: in-flight calls fail with ShuttingDown, streams
    // end, and the session keeps running (FR-020).
    void stop();

    int port() const;
    std::string url() const; // http://127.0.0.1:N/mcp

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace cutenes::mcp
