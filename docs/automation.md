# Automation: MCP and the API

cuteNES can be driven by programs as well as people: an AI agent over MCP
(the Model Context Protocol), or a script over a local JSON-RPC API. The
same commands serve both, and the desktop application exposes every menu
action as one, so anything a player can do, automation can do too
("automation parity", in the constitution).

There are two ways to run it:

- **`cutenes-server`**, a headless server with one emulator session and no
  window. This document covers it.
- **The desktop application**, with the same servers behind
  Settings > Servers. The [user guide](user-guide.md) ("Connecting an AI
  agent") covers it; `cutenes-server --attach` (below) connects a stdio
  client to it.

The full contracts are in feature 002's spec:
[`command-catalog.md`](../specs/002-ai-mcp-server/contracts/command-catalog.md)
lists every command, and
[`transports.md`](../specs/002-ai-mcp-server/contracts/transports.md) the
flags, errors and events.

## The server

`cutenes-server` (built at `build/server/cutenes-server`) puts the core
behind MCP and a JSON-RPC API. One process holds one emulator session, and
every client shares it. The headless catalog has 54 commands: `core.*` (the
whole core API), `session.*` (status, save slots, blobs, debug dumps) and
`api.*` (version and catalog).

### MCP for AI agents (stdio)

With no flags the server speaks MCP on stdin/stdout and opens no socket.
Register it as a stdio server. In Claude Code:

```bash
claude mcp add cuteNES -- /path/to/cutenes-server --library /path/to/roms
```

Or in any client's standard config:

```json
{ "mcpServers": { "cuteNES": {
    "command": "/path/to/cutenes-server",
    "args": ["--library", "/path/to/roms"] } } }
```

`core.load_rom` takes paths relative to `--library` and rejects paths that
leave it. Without `--library`, ROMs can only be uploaded as blobs.
`core.screen` returns a PNG image block. The server serves MCP revisions
2026-07-28 (`server/discover`) and 2025-11-25 (`initialize`). It has been
checked with Claude Code (see
[`quickstart.md`](../specs/002-ai-mcp-server/quickstart.md)). The server
exits at the end of its input, and calls still running stop at a frame
boundary.

### API for scripts (HTTP)

```bash
cutenes-server --library ~/roms --api-port 8765 --token-file ./api.token &
TOKEN=$(jq -r .token api.token)
rpc() { curl -s -H "Authorization: Bearer $TOKEN" localhost:8765/api/v1/rpc -d "$1"; }
rpc '{"jsonrpc":"2.0","id":1,"method":"core.load_rom","params":{"path":"game.nes"}}'
rpc '{"jsonrpc":"2.0","id":2,"method":"core.run_frames","params":{"frames":120,"script":[["Start"]]}}'
rpc '{"jsonrpc":"2.0","id":3,"method":"core.peek","params":{"addr":24576,"length":16}}'
rpc '{"jsonrpc":"2.0","id":4,"method":"api.catalog"}'   # every command with its JSON Schemas
```

The endpoint is JSON-RPC 2.0 at `POST /api/v1/rpc`, with batches allowed.
`GET /api/v1/events` is a Server-Sent Events stream of state, frame and
catalog changes. `--mcp-port N` also serves MCP over Streamable HTTP at
`/mcp`, with its own token file (`--mcp-token-file`).

### Connecting to the desktop application (`--attach`)

The desktop application runs the same API and MCP endpoints behind
Settings > Servers, both off by default. Many MCP clients only start stdio
servers, so `cutenes-server --attach` relays a stdio client to the running
application, and follows it to a new token or port when the application
restarts:

```bash
claude mcp add cuteNES -- /path/to/cutenes-server --attach
# Linux AppImage: the server is inside the image
claude mcp add cuteNES -- /path/to/cuteNES-<version>-linux-x86_64.AppImage server --attach
```

The client can start before the application does: it gets the tools once
the application is open with its MCP server on. To turn the MCP server on
for one run without changing the setting, start the application with
`cutenes --mcp` (see the user guide's "Command line").

## Versioning

The command surface has a `MAJOR.MINOR` version, currently 0.6. `api.version`
and MCP's `serverInfo.version` report it. It is separate from the
application's version and the core's
([`source-control.md`](source-control.md), "Versions and tags").

- Adding a command or an optional field bumps MINOR.
- Removing or renaming anything, or changing a field's type or meaning,
  bumps MAJOR.
- The committed catalog `tests/api/catalog.golden.json` fails the build when
  a schema changes without a version bump.

Before 1.0, a MAJOR of 0 means anything may still change.

## Security defaults

- **No network listener unless asked for.** With no flags, MCP runs over
  stdio only.
- **Loopback only.** HTTP listeners bind to `127.0.0.1` unless `--bind ADDR`
  is given.
- **Per-launch bearer tokens.** Each listener has its own token, which every
  request must carry (otherwise HTTP 401). The token file is JSON
  (`{"url", "token"}`), readable only by its owner, and is removed on a clean
  exit.
  - Default location: `~/Library/Application Support/cutenes/` on macOS,
    `$XDG_STATE_HOME/cutenes/` (or `~/.local/state/cutenes/`) on Linux, and
    `%LOCALAPPDATA%\cutenes\` on Windows.
  - Tokens are never logged.
- **Host and Origin checks.** A non-loopback `Host` or a foreign `Origin` is
  rejected with 403, which blocks DNS rebinding.
- **Bounded transfers.**
  - Reads are capped at 64 KiB per call and paged with `offset`/`length`.
  - Larger data moves through blob handles.
  - ROM paths cannot leave the library.
