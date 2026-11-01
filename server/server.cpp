#include "server.hpp"

#include <cutenes/api/http_server.hpp>
#include <cutenes/api/session.hpp>
#include <cutenes/api/token.hpp>
#include <cutenes/mcp/bridge.hpp>
#include <cutenes/mcp/http.hpp>
#include <cutenes/mcp/server.hpp>
#include <cutenes/mcp/stdio.hpp>
#include <cutenes/version.hpp>

#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <ostream>
#include <thread>

namespace cutenes::server {

namespace {

const char* const kUsage =
    "usage: cutenes-server [--library DIR] [--debug-dir DIR]\n"
    "                      [--mcp-stdio] [--mcp-port N] [--mcp-token-file PATH]\n"
    "                      [--api-port N] [--token-file PATH] [--bind ADDR]\n"
    "                      [--attach [URL]]\n"
    "\n"
    "With no listener flag it serves MCP on stdin/stdout and opens no socket.\n"
    "\n"
    "  --library DIR          ROM library root for core.load_rom paths\n"
    "  --debug-dir DIR        where session.write_dump writes (default debug)\n"
    "  --mcp-stdio            serve MCP on stdio as well as the listeners\n"
    "  --mcp-port N           serve MCP at http://127.0.0.1:N/mcp (0 = any port)\n"
    "  --mcp-token-file PATH  where to write the MCP token file\n"
    "                         (default <user state dir>/cutenes/mcp.token)\n"
    "  --api-port N           serve the API at http://127.0.0.1:N/api/v1 (0 = any port)\n"
    "  --token-file PATH      where to write the API token file\n"
    "                         (default <user state dir>/cutenes/api.token)\n"
    "  --bind ADDR            listen on ADDR instead of loopback (network exposure)\n"
    "  --attach [URL]         no session: relay stdio MCP to a running instance's MCP\n"
    "                         endpoint, found through --mcp-token-file (URL overrides\n"
    "                         the file's)\n";

struct Options {
    std::string library;
    std::string debugDir = "debug";
    std::optional<int> apiPort;
    std::optional<int> mcpPort;
    bool mcpStdio = false;
    std::string bind = "127.0.0.1";
    std::string tokenFile;
    std::string mcpTokenFile;
    bool help = false;
    bool attach = false;
    std::string attachUrl;
};

bool parsePort(const std::string& text, int& port)
{
    try {
        std::size_t used = 0;
        const int value = std::stoi(text, &used);
        if (used != text.size() || value < 0 || value > 65535) {
            return false;
        }
        port = value;
        return true;
    } catch (...) {
        return false;
    }
}

std::optional<Options> parse(const std::vector<std::string>& args, std::ostream& err)
{
    Options o;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        const auto value = [&]() -> std::optional<std::string> {
            if (i + 1 >= args.size()) {
                err << "cutenes-server: " << a << " needs a value\n";
                return std::nullopt;
            }
            return args[++i];
        };
        if (a == "--help" || a == "-h") {
            o.help = true;
        } else if (a == "--library" || a == "--debug-dir" || a == "--bind" || a == "--token-file"
            || a == "--mcp-token-file") {
            const auto v = value();
            if (!v) return std::nullopt;
            (a == "--library" ? o.library : a == "--debug-dir" ? o.debugDir
                : a == "--bind" ? o.bind : a == "--token-file" ? o.tokenFile : o.mcpTokenFile) = *v;
        } else if (a == "--api-port" || a == "--mcp-port") {
            const auto v = value();
            int port = 0;
            if (!v || !parsePort(*v, port)) {
                err << "cutenes-server: " << a << " needs a port number (0-65535)\n";
                return std::nullopt;
            }
            (a == "--api-port" ? o.apiPort : o.mcpPort) = port;
        } else if (a == "--mcp-stdio") {
            o.mcpStdio = true;
        } else if (a == "--attach") {
            o.attach = true;
            if (i + 1 < args.size() && args[i + 1].rfind("--", 0) != 0) o.attachUrl = args[++i];
        } else {
            err << "cutenes-server: unknown option " << a << "\n";
            return std::nullopt;
        }
    }
    return o;
}

// One HTTP listener's token file: written once it listens, removed on a
// clean exit (FR-024).
class TokenFile {
public:
    bool write(const std::filesystem::path& path, const std::string& url, const std::string& token,
        std::string& error)
    {
        if (!api::token::writeFile(path, url, token, error)) return false;
        m_path = path;
        return true;
    }
    ~TokenFile()
    {
        if (!m_path.empty()) {
            std::error_code ec;
            std::filesystem::remove(m_path, ec);
        }
    }
    const std::filesystem::path& path() const { return m_path; }

private:
    std::filesystem::path m_path;
};

std::filesystem::path tokenPath(const std::string& flag, const char* defaultName)
{
    return flag.empty() ? api::token::defaultDirectory() / defaultName : std::filesystem::path(flag);
}

// --attach: relay stdio to a running instance (the GUI with its MCP setting
// on, or cutenes-server --mcp-port). With none running yet, the bridge says
// so and waits for one: an MCP client starts it once and does not retry.
// Exit 4 only for an endpoint no instance could ever have (not http://).
int attach(const Options& o, std::istream& in, std::ostream& out, std::ostream& err,
    const std::atomic<bool>& stop)
{
    if (o.apiPort || o.mcpPort || o.mcpStdio || !o.library.empty()) {
        err << "cutenes-server: --attach runs no session, so it takes no listener or "
               "--library flags\n"
            << kUsage;
        return kExitUsage;
    }
    const auto path = tokenPath(o.mcpTokenFile, "mcp.token");
    const char* const remedy = "Turn on the \"MCP server\" setting in cuteNES, or start "
                               "cutenes-server --mcp-port N. The bridge waits for it.";
    if (!o.attachUrl.empty() && o.attachUrl.rfind("http://", 0) != 0) {
        err << "cutenes-server: --attach reaches http:// endpoints only, not " << o.attachUrl << "\n";
        return kExitNoEndpoint;
    }
    std::string error;
    auto endpoint = mcp::readTokenFile(path, error);
    if (endpoint && !o.attachUrl.empty()) endpoint->url = o.attachUrl;
    const int status = endpoint ? mcp::probe(*endpoint, error) : -1;
    if (status == 0) {
        err << "cutenes-server " << coreVersionString() << ": relaying stdio MCP to "
            << endpoint->url << std::endl;
    } else if (status == 401) {
        err << "cutenes-server: --attach: " << endpoint->url << " refused the token in "
            << path.string() << " (HTTP 401); the file is from an earlier launch of the "
               "endpoint. The bridge waits for a new one."
            << std::endl;
    } else if (endpoint) {
        err << "cutenes-server: --attach found no MCP endpoint yet: " << error << " (from "
            << path.string() << "). " << remedy << std::endl;
    } else {
        err << "cutenes-server: --attach found no MCP endpoint yet: " << error << ". " << remedy
            << std::endl;
    }
    // The instance's MCP server may restart with a new token or port (the
    // GUI setting toggled, the application reopened): read the file again.
    const std::string urlOverride = o.attachUrl;
    mcp::serveBridge(endpoint.value_or(mcp::Endpoint{}), in, out, stop, [path, urlOverride]() -> std::optional<mcp::Endpoint> {
        std::string ignored;
        auto fresh = mcp::readTokenFile(path, ignored);
        if (fresh && !urlOverride.empty()) fresh->url = urlOverride;
        return fresh;
    });
    return kExitOk;
}

} // namespace

int run(const std::vector<std::string>& args, std::istream& in, std::ostream& out,
    std::ostream& err, const std::atomic<bool>& stop)
{
    const auto parsed = parse(args, err);
    if (!parsed) {
        err << kUsage;
        return kExitUsage;
    }
    const Options& o = *parsed;
    if (o.help) {
        err << kUsage;
        return kExitOk;
    }
    if (o.attach) {
        return attach(o, in, out, err, stop);
    }
    // Launching is the opt-in (FR-009): stdio unless a listener was asked
    // for, and then stdio only when asked too.
    const bool stdio = o.mcpStdio || (!o.apiPort && !o.mcpPort);

    api::SessionConfig config;
    config.libraryRoot = o.library;
    config.debugDir = o.debugDir;
    api::Session session(config);
    mcp::McpServer mcp(session);

    // Each listener gets its own token; the token files carry them, and the
    // log carries only where the files are.
    std::unique_ptr<api::HttpServer> apiServer;
    std::unique_ptr<mcp::McpHttpServer> mcpServer;
    TokenFile apiToken, mcpToken;
    std::string error;
    if (o.apiPort) {
        api::HttpServerConfig http;
        http.bindAddress = o.bind;
        http.port = *o.apiPort;
        http.token = api::token::generate();
        if (http.token.empty()) {
            err << "cutenes-server: the OS random source failed\n";
            return kExitBind;
        }
        apiServer = std::make_unique<api::HttpServer>(session, http);
        if (!apiServer->start(error)
            || !apiToken.write(tokenPath(o.tokenFile, "api.token"), apiServer->url(), http.token, error)) {
            err << "cutenes-server: " << error << "\n";
            return kExitBind;
        }
        err << "cutenes-server " << coreVersionString() << ": API at " << apiServer->url()
            << "; token file " << apiToken.path().string() << std::endl;
    }
    if (o.mcpPort) {
        mcp::McpHttpConfig http;
        http.bindAddress = o.bind;
        http.port = *o.mcpPort;
        http.token = api::token::generate();
        if (http.token.empty()) {
            err << "cutenes-server: the OS random source failed\n";
            return kExitBind;
        }
        mcpServer = std::make_unique<mcp::McpHttpServer>(mcp, http);
        if (!mcpServer->start(error)
            || !mcpToken.write(tokenPath(o.mcpTokenFile, "mcp.token"), mcpServer->url(), http.token, error)) {
            err << "cutenes-server: " << error << "\n";
            return kExitBind;
        }
        err << "cutenes-server " << coreVersionString() << ": MCP at " << mcpServer->url()
            << "; token file " << mcpToken.path().string() << std::endl;
    }
    if ((apiServer || mcpServer) && !api::isLoopbackHost(o.bind)) {
        err << "cutenes-server: warning: listening on " << o.bind
            << ", reachable from the network; the token is the only protection\n";
    }

    if (stdio) {
        err << "cutenes-server " << coreVersionString() << ": MCP on stdio" << std::endl;
        // Ends at end of input (the client's shutdown) or on a signal.
        mcp::serveStdio(mcp, in, out, stop);
    } else {
        while (!stop.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
    if (mcpServer) mcpServer->stop();
    if (apiServer) apiServer->stop();
    return kExitOk;
}

} // namespace cutenes::server
