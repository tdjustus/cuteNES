# Contract: Run modes, API protocol and MCP mapping

**Feature**: 002 | **Authority**: spec FR-001, FR-008, FR-009, FR-013, FR-020-FR-023;
research R3-R5, R11

## Run modes (FR-021)

### Headless: `cutenes-server`

```
cutenes-server [--library DIR] [--mcp-stdio] [--mcp-port N] [--api-port N]
               [--bind ADDR] [--attach URL]
```

| Flag | Effect | Default |
|---|---|---|
| (none) | MCP over stdio, no listener | on |
| `--library DIR` | ROM library root for `core.load_rom` paths | none: uploads only |
| `--api-port N` | Serve the API at `http://127.0.0.1:N/api/v1` | off |
| `--mcp-port N` | Serve MCP Streamable HTTP at `http://127.0.0.1:N/mcp` | off |
| `--bind ADDR` | Bind HTTP listeners to `ADDR` instead of loopback (FR-008 network exposure) | loopback |
| `--attach URL` | **Bridge mode**: no session; relay stdio MCP to a running GUI's MCP endpoint | off |

- Launching is the opt-in (FR-009). With no flags it opens no socket.
- One session per process. In headless mode the catalog is `core.*`,
  `session.*` and `api.*`.
- Exit codes: 0 on a clean stdin EOF, 2 on a usage error, 3 when a bind
  fails, 4 when `--attach` finds no endpoint (the GUI's MCP toggle is off).
  The `--attach` message names the setting to enable.

### GUI: the Qt application (feature 003)

- Host: the application's `IHost` registers `gui.*` commands (FR-023).
- The API and MCP HTTP listeners follow the two settings toggles. Both are
  off by default, and MCP requires the API (FR-009, FR-020). Ports are
  settings.
- Stdio-only MCP clients register `cutenes-server --attach
  http://127.0.0.1:PORT/mcp`.
- Disabling the API closes the MCP endpoint. In-flight and queued calls
  fail with `ShuttingDown`, open streams end, and emulation continues
  (FR-020).

## API protocol (FR-022)

- **Endpoint**: `POST /api/v1/rpc`, `Content-Type: application/json`, with a
  JSON-RPC 2.0 request or batch. `method` is the command name and `params`
  is its arguments object.
- **Events**: `GET /api/v1/events` is a Server-Sent Events stream. Events are
  `state_changed` (load, eject, reset, power, state load, CPU or memory
  write), `run_state_changed`, `frame` (at most 10 per second, with
  `frame`/`frameHash`) and `catalog_changed` (host commands added or
  removed).
- **Discovery**: `api.catalog` returns every command with its `inputSchema`
  and `outputSchema` (JSON Schema 2020-12).
- **Security** (R11): loopback bind by default. `Origin`, if present, must
  be a loopback origin. A non-loopback `Host` header is rejected with 403.
- **Ordering** (FR-007): calls from all clients and the GUI execute in
  arrival order on the session's command queue, one at a time and each to
  completion. A batch executes in array order and is not atomic.

Example:

```json
{"jsonrpc":"2.0","id":1,"method":"core.run_frames",
 "params":{"frames":60,"script":[["Start"]]}}
```

## MCP mapping (FR-001, FR-004)

| MCP | Behavior |
|---|---|
| `server/discover` (2026-07-28) | Supported versions `["2026-07-28","2025-11-25"]`, capabilities `{ tools: { listChanged: true } }`, `serverInfo` (name `cutenes`, version = surface version) |
| `initialize` (2025-11-25) | Negotiates 2025-11-25 for that connection; afterwards behaves as 2025-11-25 |
| `tools/list` | One tool per catalog command, in the catalog's deterministic order. `inputSchema`/`outputSchema` come from the registry. Carries `ttlMs` (60000) and `cacheScope: "private"` (2026-07-28) |
| `tools/call` | Dispatches to the command. The result has `structuredContent` (the command's result) plus a text summary. `core.screen` with `png` adds an `image` content block |
| `subscriptions/listen` (2026-07-28) / `notifications/tools/list_changed` (2025-11-25) | Sent when host commands change (`catalog_changed`) |
| Tool error | `isError: true` with `structuredContent` `{ name, message, data }` (command-catalog "Errors") |

- Per-request `_meta` version and capabilities (2026-07-28) are read on every
  request. An unsupported version returns `UnsupportedProtocolVersion`
  (`-32022`).
- Handles (`uploadId`, `stateId`, slot names) are explicit tool arguments.
  There is no protocol session state (2026-07-28 SEP-2567).
- stdio: newline-delimited JSON-RPC, with nothing but protocol on stdout.
  Logs go to stderr.
- Streamable HTTP: a single `POST /mcp` endpoint that requires
  `Mcp-Method`/`Mcp-Name`, has the same loopback and Origin rules as the API,
  and returns a JSON response, or SSE for runs long enough to emit
  `notifications/progress`.
- Disconnect (FR-013): a broken stream cancels that request at the next frame
  boundary with `Cancelled`. Nothing else in the session changes.
