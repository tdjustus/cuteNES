#include "server.hpp"

#include <cutenes/api/http_server.hpp>
#include <cutenes/api/session.hpp>
#include <cutenes/api/token.hpp>
#include <cutenes/version.hpp>

#include <chrono>
#include <filesystem>
#include <optional>
#include <ostream>
#include <thread>

namespace cutenes::server {

namespace {

const char* const kUsage =
    "usage: cutenes-server [--library DIR] [--debug-dir DIR]\n"
    "                      [--api-port N] [--bind ADDR] [--token-file PATH]\n"
    "                      [--mcp-stdio] [--mcp-port N] [--attach [URL]]\n"
    "\n"
    "  --library DIR      ROM library root for core.load_rom paths\n"
    "  --debug-dir DIR    where session.write_dump writes (default debug)\n"
    "  --api-port N       serve the API at http://127.0.0.1:N/api/v1 (0 = any port)\n"
    "  --bind ADDR        listen on ADDR instead of loopback (network exposure)\n"
    "  --token-file PATH  where to write the API token file\n"
    "                     (default <user state dir>/cutenes/api.token)\n";

struct Options {
    std::string library;
    std::string debugDir = "debug";
    std::optional<int> apiPort;
    std::string bind = "127.0.0.1";
    std::string tokenFile;
    bool help = false;
    std::string pending; // a mode that a later phase of feature 002 adds
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
        } else if (a == "--library" || a == "--debug-dir" || a == "--bind" || a == "--token-file") {
            const auto v = value();
            if (!v) return std::nullopt;
            (a == "--library" ? o.library : a == "--debug-dir" ? o.debugDir
                : a == "--bind" ? o.bind : o.tokenFile) = *v;
        } else if (a == "--api-port") {
            const auto v = value();
            int port = 0;
            if (!v || !parsePort(*v, port)) {
                err << "cutenes-server: --api-port needs a port number (0-65535)\n";
                return std::nullopt;
            }
            o.apiPort = port;
        } else if (a == "--mcp-stdio" || a == "--mcp-port" || a == "--attach") {
            o.pending = a;
            if (a == "--mcp-port" && i + 1 < args.size()) ++i;
            if (a == "--attach" && i + 1 < args.size() && args[i + 1].rfind("--", 0) != 0) ++i;
        } else {
            err << "cutenes-server: unknown option " << a << "\n";
            return std::nullopt;
        }
    }
    return o;
}

} // namespace

int run(const std::vector<std::string>& args, std::ostream& err, const std::atomic<bool>& stop)
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
    // MCP (stdio by default, --mcp-port, --attach) arrives in feature 002
    // phases P3 and P4. Until then nothing is served without --api-port, so
    // the default still opens no socket (FR-009).
    if (!o.pending.empty() || !o.apiPort) {
        err << "cutenes-server: "
            << (o.pending.empty() ? std::string("MCP over stdio (the default mode)") : o.pending)
            << " is not available yet; use --api-port N\n";
        return kExitUsage;
    }

    api::SessionConfig config;
    config.libraryRoot = o.library;
    config.debugDir = o.debugDir;
    api::Session session(config);

    api::HttpServerConfig http;
    http.bindAddress = o.bind;
    http.port = *o.apiPort;
    http.token = api::token::generate();
    if (http.token.empty()) {
        err << "cutenes-server: the OS random source failed\n";
        return kExitBind;
    }
    api::HttpServer server(session, http);
    std::string error;
    if (!server.start(error)) {
        err << "cutenes-server: " << error << "\n";
        return kExitBind;
    }
    const std::filesystem::path tokenFile = o.tokenFile.empty()
        ? api::token::defaultDirectory() / "api.token"
        : std::filesystem::path(o.tokenFile);
    if (!api::token::writeFile(tokenFile, server.url(), http.token, error)) {
        err << "cutenes-server: " << error << "\n";
        return kExitBind;
    }
    if (!api::isLoopbackHost(o.bind)) {
        err << "cutenes-server: warning: listening on " << o.bind
            << ", reachable from the network; the token is the only protection\n";
    }
    // The token file's location, never the token (FR-024).
    err << "cutenes-server " << coreVersionString() << ": API at " << server.url()
        << "; token file " << tokenFile.string() << std::endl;

    while (!stop.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    server.stop();
    std::error_code ec;
    std::filesystem::remove(tokenFile, ec); // removed on clean exit
    return kExitOk;
}

} // namespace cutenes::server
