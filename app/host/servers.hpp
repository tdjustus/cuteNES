#pragma once

// Settings > Servers (spec FR-071-FR-073, data-model "ServerSettings"): the
// API and the MCP server of the window's own session, both off until the
// user turns them on. MCP needs the API. Each listener gets a fresh token
// per start, written to its token file (api.token, mcp.token) for
// `cutenes-server --attach`; the files go when the listener stops. GUI
// thread only: stopping a listener waits for its threads.

#include <cutenes/api/http_server.hpp>

#include <QString>

#include <filesystem>
#include <memory>

namespace cutenes::api {
class Session;
}
namespace cutenes::mcp {
class McpServer;
class McpHttpServer;
}

namespace cutenes::app {

class Servers {
public:
    // tokenDir: where the token files go (the user's state folder, or a
    // test's own).
    Servers(api::Session& session, std::filesystem::path tokenDir);
    ~Servers();
    Servers(const Servers&) = delete;
    Servers& operator=(const Servers&) = delete;

    struct Listener {
        bool on = false;
        QString url;       // while on
        QString token;     // while on
        QString tokenFile; // while on
    };

    // Starts or stops each listener to match; a port of 0 picks a free one.
    // A listener that is on stays as it is (its port changes on the next
    // start). MCP on starts the API too. False, with a reason, when a
    // listener could not start; it stays off.
    bool apply(bool api, int apiPort, bool mcp, int mcpPort, QString* error = nullptr);
    void stopAll();

    Listener api() const;
    Listener mcp() const;
    // Both listeners together: open streams, calls running, the last call.
    api::ListenerActivity activity() const;

private:
    void stopMcp();
    void stopApi();

    api::Session& m_session;
    std::filesystem::path m_tokenDir;
    std::unique_ptr<api::HttpServer> m_api;
    std::string m_apiToken;
    std::unique_ptr<mcp::McpServer> m_mcp;
    std::unique_ptr<mcp::McpHttpServer> m_mcpHttp;
    std::string m_mcpToken;
};

} // namespace cutenes::app
