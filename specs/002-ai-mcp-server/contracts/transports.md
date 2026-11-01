# Contract: Run modes, API protocol and MCP mapping

**Feature**: 002 | **Authority**: spec FR-001, FR-008, FR-009, FR-013, FR-020-FR-023;
research R3-R5, R11

## Run modes (FR-021)

### Headless: `cutenes-server`

```
cutenes-server [--library DIR] [--debug-dir DIR]
               [--mcp-stdio] [--mcp-port N] [--mcp-token-file PATH]
               [--api-port N] [--token-file PATH] [--bind ADDR]
               [--attach [URL]]
```

| Flag | Effect | Default |
|---|---|---|
| (none) | MCP over stdio, no listener | on |
| `--library DIR` | ROM library root for `core.load_rom` paths | none: uploads only |
| `--debug-dir DIR` | Where `session.write_dump` writes | `debug` |
| `--mcp-stdio` | Serve MCP on stdio as well as the listeners asked for | off, unless no listener flag is given |
| `--mcp-port N` | Serve MCP Streamable HTTP at `http://127.0.0.1:N/mcp` (`0` picks a free port; the MCP token file has the URL) | off |
| `--mcp-token-file PATH` | Where to write the MCP token file (FR-024) | `<user state dir>/cutenes/mcp.token` |
| `--api-port N` | Serve the API at `http://127.0.0.1:N/api/v1` (`0` picks a free port; the API token file has the URL) | off |
| `--token-file PATH` | Where to write the API token file (FR-024) | `<user state dir>/cutenes/api.token` |
| `--bind ADDR` | Bind HTTP listeners to `ADDR` instead of loopback (FR-008 network exposure) | loopback |
| `--attach [URL]` | **Bridge mode**: no session; relay stdio MCP to a running instance's MCP endpoint, found through `--mcp-token-file` (a `URL` replaces the file's) | off |

- Launching is the opt-in (FR-009). With no flags it opens no socket.
- MCP on stdio runs when no listener flag is given, or with `--mcp-stdio`.
  Then the server exits at the end of its input, and in-flight calls are
  cancelled at the next frame boundary, as for any client that goes away.
  With listeners only, it runs until SIGINT/SIGTERM.
- One session per process: stdio, the MCP endpoint and the API share it.
  In headless mode the catalog is `core.*`, `session.*` and `api.*`.
- Exit codes: 0 on a clean stdin EOF or on SIGINT/SIGTERM, 2 on a usage
  error, 3 when a bind fails (or a token file cannot be written), 4 when
  `--attach` is given a URL that is not `http://`. With no endpoint yet (no
  token file, nothing answering, or a token refused with HTTP 401) the
  bridge says so on stderr and waits for one: an MCP client starts its
  servers once and does not start one again after it exits.
- `--attach` takes no listener, `--mcp-stdio` or `--library` flag (exit 2).
  It checks the endpoint with `server/discover` before relaying. While no
  endpoint answers, it answers a 2025-11-25 client's `initialize`,
  `tools/list` (no tools) and `ping` itself, and other requests fail with
  a message naming the setting to turn on. When an endpoint answers after
  that, or a new one after a restart, the client gets
  `notifications/tools/list_changed`. Requests
  go to the endpoint one at a time in arrival order. A
  `subscriptions/listen` runs on its own connection. `notifications/cancelled`
  closes the request's connection, which is HTTP's cancellation. A
  2025-11-25 client gets `notifications/tools/list_changed` through a
  subscription the bridge holds for it. End of input cancels what is
  running, as for any client.
- Each HTTP listener has its own token, generated at launch. On start the
  server prints each listener's URL and token file path to stderr, never a
  token. A token file is JSON, `{"url": ..., "token": ...}`, owner-only
  (mode 0600; a protected owner-only DACL on Windows), replaced on each
  start and removed on a clean exit.
  The `--attach` message names the setting to enable ("MCP server") and the
  token file it read.

### GUI: the Qt application (feature 003)

- Host: the application's `IHost` registers `gui.*` commands (FR-023).
- **Real-time play** (003 research R3). The GUI turns on the session's
  real-time mode (`Session::setRealtime`). Its clock calls `requestFrames`,
  and the emulation thread runs one frame per request while the machine is
  `Running`, between queued commands. Human input goes through
  `setLiveInput`, and at each frame start it is combined (OR) with the last
  `core.set_controller` value. A scripted `core.run_frames` uses its script
  alone. Headless sessions never enable this mode.
- The API and MCP HTTP listeners follow the two settings toggles. Both are
  off by default, and MCP requires the API (FR-009, FR-020). Ports are
  settings.
- Stdio-only MCP clients register `cutenes-server --attach`. With no URL it
  reads the GUI's MCP token file, which holds the URL and token. The GUI's
  server settings show the token and offer to copy it, for HTTP clients
  configured by hand.
- The GUI makes a new token, and may take a new port, each time its MCP
  server starts. A running bridge follows it: when the endpoint refuses its
  token (401) or does not answer, it reads the token file again and, if
  the file names a different endpoint, sends the request once more there.
  Its own list-changed subscription reconnects the same way. A request
  that had reached the endpoint is never sent twice.
- Disabling the API closes the MCP endpoint. In-flight and queued calls
  fail with `ShuttingDown`, open streams end, and emulation continues
  (FR-020).

## API protocol (FR-022)

- **Endpoint**: `POST /api/v1/rpc`, `Content-Type: application/json`, with a
  JSON-RPC 2.0 request or batch. `method` is the command name and `params`
  is its arguments object.
- **Responses**: a batch made only of notifications, or a single
  notification, gets `204 No Content`. Parameters are by name only (an
  array `params` is `-32602`).
- **Errors**: the JSON-RPC `error` is `{ code, message, data }`, and
  `data.name` is the command-catalog error name next to its details.
  `UnknownCommand` is `-32601`, `InvalidArguments` `-32602`, `InternalError`
  `-32603`; the rest are `NoCartridge -32001`, `LoadError -32002`,
  `LoadStateError -32003`, `WriteError -32004`, `PathOutsideLibrary -32005`,
  `LimitExceeded -32006`, `UnknownHandle -32007`, `Cancelled -32008`,
  `ShuttingDown -32009`, `NotFound -32010`, `NoBattery -32011`,
  `NotImplemented -32012`.
- **Disconnect**: a client that closes its connection mid-call cancels it at
  the next frame boundary (FR-013), as for MCP.
- **Events**: `GET /api/v1/events` is a Server-Sent Events stream. Events are
  `state_changed` (load, eject, reset, power, state load, CPU or memory
  write), `run_state_changed`, `frame` (at most 10 per second, with
  `frame`/`frameHash`) and `catalog_changed` (host commands added or
  removed). Each `data` is a JSON object with the event's fields plus `frame`
  and `masterCycle`. The stream starts with a `: connected` comment and
  sends a `: keepalive` comment after 15 s without events; a client that
  falls 256 events behind loses the oldest.
- **Discovery**: `api.catalog` returns every command with its `inputSchema`
  and `outputSchema` (JSON Schema 2020-12).
- **Security** (R11): loopback bind by default. Every request needs
  `Authorization: Bearer <token>` (FR-024); without it the response is 401.
  `Origin`, if present, must be a loopback origin. A non-loopback `Host`
  header is rejected with 403.
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

- Revision per request: a request whose `_meta` carries
  `io.modelcontextprotocol/protocolVersion` is served under that version,
  statelessly. An unsupported version returns `UnsupportedProtocolVersion`
  (`-32022`, `data: {supported, requested}`). `initialize` makes a stdio
  process 2025-11-25 for its requests without `_meta`; on HTTP the
  `MCP-Protocol-Version: 2025-11-25` header does the same per request. A
  request with neither is `-32602`.
- Handles (`blobId`, slot names) are explicit tool arguments.
  There is no protocol session state (2026-07-28 SEP-2567).
- `tools/list` returns the whole catalog in one page, with
  `annotations.readOnlyHint` for commands that change nothing. Finding the
  tool is a protocol error (unknown tool: `-32602`); everything after is a
  tool result. A success's text block is the result as JSON, with
  `core.screen`'s PNG replaced by a note, since the PNG is in its own image
  block. Batches are `-32600` (MCP removed them).
- `notifications/progress` for `core.run_frames` when the request's `_meta`
  has a `progressToken`: at most every 100 ms, `progress` frames run of
  `total`.
- stdio: newline-delimited JSON-RPC, with nothing but protocol on stdout.
  Logs go to stderr. A client cancels with `notifications/cancelled`; no
  response follows. `ping` is answered for 2025-11-25 clients.
- Streamable HTTP: a single `POST /mcp` endpoint behind the bearer token and
  the API's loopback and Origin rules. A 2026-07-28 request must send
  `MCP-Protocol-Version` (equal to its `_meta` version), `Mcp-Method`, and,
  for `tools/call`, `Mcp-Name` (plain or `=?base64?...?=`); a missing or
  mismatched header is `400` with `HeaderMismatch` (`-32020`). An
  unsupported version is `400`, an unknown method `404` with `-32601`, a
  notification `202`, and `GET`/`DELETE` `405`. A request gets a JSON
  response, or an SSE stream (`X-Accel-Buffering: no`) when it is a
  `tools/call` with a `progressToken` (progress, then the result) or
  `subscriptions/listen` (the acknowledgement, list changes, keep-alive
  comments, and the closing response when the endpoint stops).
- Disconnect (FR-013): a broken stream cancels that request at the next frame
  boundary with `Cancelled`. Nothing else in the session changes. Closing
  the endpoint fails in-flight calls with `ShuttingDown`.
