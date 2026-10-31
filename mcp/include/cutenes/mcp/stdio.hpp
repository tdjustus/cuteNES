#pragma once

// The MCP stdio transport: newline-delimited JSON-RPC, one message per
// line, with nothing but protocol on `out` (logs go to stderr).

#include <cutenes/mcp/server.hpp>

#include <atomic>
#include <iosfwd>

namespace cutenes::mcp {

// Serves until `in` reaches end of file (the client's shutdown signal) or
// `stop` becomes true. On either, in-flight calls are cancelled at the next
// frame boundary (FR-013) and open subscriptions end. Returns true on end of
// file.
bool serveStdio(McpServer& server, std::istream& in, std::ostream& out,
    const std::atomic<bool>& stop);

} // namespace cutenes::mcp
