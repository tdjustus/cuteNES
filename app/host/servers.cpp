#include "host/servers.hpp"

#include <cutenes/api/token.hpp>
#include <cutenes/mcp/bridge.hpp>
#include <cutenes/mcp/http.hpp>
#include <cutenes/mcp/server.hpp>

#include <QDir>
#include <QObject>

#include <algorithm>
#include <system_error>

namespace cutenes::app {

namespace {

const char* const kApiTokenFile = "api.token";
const char* const kMcpTokenFile = "mcp.token";

// Every cuteNES on the machine shares the token files and the latest to
// start a server owns them, so a file goes only while it holds `token`:
// never one of another instance, which a client may be attached to.
void removeOwnFile(const std::filesystem::path& path, const std::string& token)
{
    std::string error;
    const auto file = mcp::readTokenFile(path, error);
    if (token.empty() || !file || file->token != token) return;
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

QString text(const std::string& s)
{
    return QString::fromStdString(s);
}

} // namespace

Servers::Servers(api::Session& session, std::filesystem::path tokenDir)
    : m_session(session), m_tokenDir(std::move(tokenDir))
{
}

Servers::~Servers() { stopAll(); }

bool Servers::apply(bool api, int apiPort, bool mcp, int mcpPort, QString* error)
{
    api = api || mcp; // MCP needs the API (FR-071)
    if (!mcp) stopMcp();
    if (!api) stopApi();
    std::string why;
    if (api && !m_api) {
        api::HttpServerConfig config;
        config.port = apiPort;
        config.token = api::token::generate();
        auto server = std::make_unique<api::HttpServer>(m_session, config);
        if (config.token.empty()) {
            why = "the system's random source failed";
        } else if (server->start(why)
            && api::token::writeFile(m_tokenDir / kApiTokenFile, server->url(), config.token, why)) {
            m_api = std::move(server);
            m_apiToken = config.token;
        } else {
            server.reset();
            removeOwnFile(m_tokenDir / kApiTokenFile, config.token);
            if (error) *error = QObject::tr("The API could not start: %1").arg(text(why));
            stopMcp();
            return false;
        }
    }
    if (mcp && !m_mcpHttp) {
        if (!m_mcp) m_mcp = std::make_unique<mcp::McpServer>(m_session);
        mcp::McpHttpConfig config;
        config.port = mcpPort;
        config.token = api::token::generate();
        auto server = std::make_unique<mcp::McpHttpServer>(*m_mcp, config);
        if (config.token.empty()) {
            why = "the system's random source failed";
        } else if (server->start(why)
            && api::token::writeFile(m_tokenDir / kMcpTokenFile, server->url(), config.token, why)) {
            m_mcpHttp = std::move(server);
            m_mcpToken = config.token;
        } else {
            server.reset();
            removeOwnFile(m_tokenDir / kMcpTokenFile, config.token);
            if (error) *error = QObject::tr("The MCP server could not start: %1").arg(text(why));
            return false;
        }
    }
    return true;
}

void Servers::stopAll()
{
    stopMcp();
    stopApi();
}

void Servers::stopMcp()
{
    // In-flight calls fail with ShuttingDown and streams end; the session
    // and the game keep running (FR-072).
    if (m_mcpHttp) m_mcpHttp->stop();
    m_mcpHttp.reset();
    m_mcp.reset();
    removeOwnFile(m_tokenDir / kMcpTokenFile, m_mcpToken);
    m_mcpToken.clear();
}

void Servers::stopApi()
{
    if (m_api) m_api->stop();
    m_api.reset();
    removeOwnFile(m_tokenDir / kApiTokenFile, m_apiToken);
    m_apiToken.clear();
}

Servers::Listener Servers::api() const
{
    if (!m_api) return {};
    return {true, text(m_api->url()), text(m_apiToken), QDir::toNativeSeparators(text((m_tokenDir / kApiTokenFile).string()))};
}

Servers::Listener Servers::mcp() const
{
    if (!m_mcpHttp) return {};
    return {true, text(m_mcpHttp->url()), text(m_mcpToken),
        QDir::toNativeSeparators(text((m_tokenDir / kMcpTokenFile).string()))};
}

api::ListenerActivity Servers::activity() const
{
    api::ListenerActivity total;
    for (const api::ListenerActivity& a : {m_api ? m_api->activity() : api::ListenerActivity{},
             m_mcpHttp ? m_mcpHttp->activity() : api::ListenerActivity{}}) {
        total.streams += a.streams;
        total.calls += a.calls;
        total.lastCall = std::max(total.lastCall, a.lastCall);
    }
    return total;
}

} // namespace cutenes::app
