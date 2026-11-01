#pragma once

// The --attach bridge (contracts/transports.md "Run modes", A053): a stdio
// MCP client relayed to a running instance's Streamable HTTP endpoint, for
// clients that can only launch stdio servers. The bridge has no session; it
// adds the headers 2026-07-28 requires and passes messages through.

#include <cutenes/mcp/server.hpp>

#include <atomic>
#include <filesystem>
#include <functional>
#include <iosfwd>
#include <optional>
#include <string>

namespace cutenes::mcp {

struct Endpoint {
    std::string url;   // http://host:port/mcp
    std::string token;
};

// {"url", "token"} from an instance's token file; nullopt (with `error`)
// when it is missing or malformed.
std::optional<Endpoint> readTokenFile(const std::filesystem::path& path, std::string& error);

// One server/discover round trip: 0 when the endpoint answers, the HTTP
// status when it refuses (401 for a token from another launch), -1 when
// nothing answers. `error` says what happened.
int probe(const Endpoint& endpoint, std::string& error);

// Relays until `in` ends or `stop` becomes true; returns true at end of
// input. Requests go to the endpoint one at a time, in arrival order
// (FR-007); a subscriptions/listen stream runs alongside. A
// notifications/cancelled drops that request's connection, which the
// endpoint treats as cancellation. A 2025-11-25 client hears list changes
// through a subscription the bridge holds for it.
//
// The instance makes a new token, and may take a new port, each time its
// endpoint starts (the GUI's MCP setting toggled, or the application
// restarted). When the endpoint refuses the token or does not answer, the
// bridge asks `refresh` (which reads the token file again) and, if it
// names a different endpoint, sends the request once more there; the
// list-changed subscription reconnects the same way.
//
// `endpoint` may be empty (no instance yet), and the instance may go away.
// While nothing answers, a 2025-11-25 client's initialize, tools/list (no
// tools) and ping are answered by the bridge, and other requests fail
// saying what to turn on. When an instance answers again (or for the first
// time after that), the client hears notifications/tools/list_changed.
using RefreshEndpoint = std::function<std::optional<Endpoint>()>;
bool serveBridge(const Endpoint& endpoint, std::istream& in, std::ostream& out,
    const std::atomic<bool>& stop, RefreshEndpoint refresh = {});

} // namespace cutenes::mcp
