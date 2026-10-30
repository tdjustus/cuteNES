# Research: API and MCP server (feature 002)

Each decision records the choice, the reason, and the alternatives rejected.
Sources are cited inline.

## R1. One command registry, two surfaces

**Decision.** All control goes through a C++ **command registry** in a new
library, `cutenes-api`. Each command has a stable name, a JSON Schema
(2020-12) for its input and output, a handler, and flags (mutating, needs a
ROM, host-only). The public API serves the registry as JSON-RPC methods. The
MCP adapter lists the same registry as MCP tools. Neither surface has
commands of its own.

**Why.** FR-022/SC-008 require the API and MCP to be impossible to diverge,
and constitution V requires parity. With one registry, catalog equality is a
property of the code rather than of review discipline.

**Rejected.** Separate hand-written MCP tools that call the API: two catalogs
to keep in sync, which is exactly the divergence FR-022 forbids. Generating C++
from a schema file: adds a build-time generator for no benefit at about 60
commands.

## R2. Implement MCP in C++, not with an SDK in another language

**Decision.** The MCP adapter is written in C++20 in `cutenes-mcp`. It
implements JSON-RPC 2.0 and the MCP methods the server needs.

**Why.** The constitution names C++20 as the project language. The headless
server must ship as one native executable per platform next to the core. GUI
mode runs the server inside the Qt application process. There is no official
MCP C++ SDK; the official SDKs are TypeScript, Python, Java, Kotlin, C#, Go,
Rust, Swift, Ruby and PHP (modelcontextprotocol.io SDK list). The server's
protocol needs are small: tools only, with no resources, prompts, sampling or
elicitation.

**Rejected.** A Python or TypeScript sidecar driving the core over the API:
it adds a runtime the user must install, and in GUI mode a second process.
Binding the core into Rust and using the Rust SDK: it adds a second
toolchain.

## R3. MCP protocol revisions: 2026-07-28, with 2025-11-25 compatibility

**Decision.** Target revision **2026-07-28**. Also accept **2025-11-25**
clients, because mainstream clients upgrade gradually.

- 2026-07-28 is stateless. There is no `initialize` handshake and no
  protocol session. Every request carries its protocol version and client
  capabilities in `_meta`, and servers MUST implement `server/discover`.
  Results carry `resultType`. Servers SHOULD return `tools/list` in a
  deterministic order, and list results carry `ttlMs`/`cacheScope`.
  Cross-call state uses "explicit, server-minted handles passed as ordinary
  tool arguments" (2026-07-28 changelog, SEP-2567, SEP-2575, SEP-2549).
- Compatibility: answer `initialize` (2025-11-25) by negotiating that
  version for the connection, and answer `server/discover` (2026-07-28). On
  stdio, `server/discover` is the documented compatibility probe.
- Deprecated features are not implemented: roots, sampling, logging and
  HTTP+SSE (2026-07-28 "Deprecated").

**Why.** The emulator's cross-call state (save slots, uploads, the audio
marker) already maps to explicit handles, so the stateless revision fits it
without workarounds.

## R4. Transports

| Mode | API | MCP |
|---|---|---|
| Headless (`cutenes-server`) | Local HTTP, JSON-RPC 2.0, only with `--api-port N` | stdio by default; Streamable HTTP with `--mcp-port N` |
| GUI (Qt app, feature 003) | Local HTTP, on when the API toggle is on | Streamable HTTP, on when the MCP toggle is on; `cutenes-server --attach` bridges stdio clients to it |

- **stdio**: newline-delimited JSON-RPC on stdin/stdout. Diagnostics go to
  stderr only (MCP stdio transport).
- **Streamable HTTP**: one POST endpoint, with a JSON or SSE response per
  request (2026-07-28 Streamable HTTP). It binds to `127.0.0.1` and `::1`,
  and validates `Origin` to prevent DNS rebinding (an MCP spec security
  requirement). It also requires the `Mcp-Method`/`Mcp-Name` headers
  (2026-07-28 minor change 4).
- **API**: `POST /api/v1/rpc` with a JSON-RPC 2.0 body (single or batch),
  plus `GET /api/v1/events` as a Server-Sent Events stream of session events
  (state changed, frame produced, host commands changed). It uses the same
  loopback-only binding and the same Origin check.
- **Network exposure** (FR-008) is a separate explicit flag (`--bind ADDR`
  headless, a setting in the GUI). Without it, only loopback is bound.

**HTTP library.** cpp-httplib (MIT, header-only, a single dependency for
both server and client, so the `--attach` bridge reuses it), pinned with
FetchContent.

**Rejected.** gRPC: heavy, a second schema language (protobuf), and awkward
for scripts. WebSocket for the API: request/response over POST plus SSE events
covers the needs, and both are trivial from Python (`requests`) and `curl`.

## R5. Execution model and serialization (FR-007)

**Decision.** A **Session** owns the single `cutenes::Machine` and a command
queue drained by one **emulation thread**. Every API call, from any transport
or client and from the GUI user alike, becomes a queued command that runs to
completion on that thread. Transports run on their own I/O threads and only
enqueue and await.

- The core is single-threaded and spawns no threads (core contract, global
  rules), so it is touched from exactly one thread.
- **Autonomous pacing** (GUI real-time play) is a host-owned loop on the same
  thread. It runs one frame, drains pending commands, and repeats. Commands
  therefore always apply between frames, never mid-frame, unless the command
  itself steps (`core.step_instruction`).
- **Advance commands** (`run_frames`, `frame_advance`, `step_instruction`)
  first pause autonomous pacing and leave it paused. The session reports
  "paused by automation" so the GUI can show it. They never interleave with
  real-time frames. This makes FR-006 determinism hold in GUI mode too.
- **Long runs.** `core.run_frames` is capped per call (default 3600 frames,
  overridable up to 216000). It checks for cancellation between frames, so a
  disconnect or a GUI stop can interrupt it at a frame boundary.

**Rejected.** A lock around the Machine: callers would interleave between
frames from different threads, and the GUI loop and automation would race for
the order of operations.

## R6. Payload encoding and bounds (FR-011, FR-012)

- **Screen capture.** PNG, RGB 8-bit, 256x240, via `stb_image_write`
  (public domain / MIT dual licence, a single header, vendored with NOTICE).
  MCP returns an `image` content block (`image/png`, base64). The API returns
  `{ "png": base64 }` or, with `format: "raw"`, XRGB8888 bytes in base64.
- **Memory, VRAM, OAM, SRAM and state blobs.** base64 in JSON, with a
  64 KiB cap per call. Larger reads chunk with `offset`/`length`.
- **Audio** (FR-016). 16-bit mono PCM in base64, from a client-supplied
  marker (a frame index or master cycle), capped at 1 s per call. The session
  keeps a bounded ring of 10 s, and reports `truncated: true` if the marker is
  older than the ring.
- **Determinism hashes.** `core.frame_hash` returns the FNV-1a hash the
  harness uses (`docs/test-corpus.md`), so clients can compare runs without
  transferring images.

## R7. Host extension (FR-023) and modes

**Decision.** `cutenes-api` defines an `IHost` interface. The host registers
its commands at startup and is told about session events. Headless mode uses
`HeadlessHost`, which registers none. GUI mode uses the Qt application's host
(feature 003), which registers one command per user-facing action under the
`gui.` namespace. A command's `host` flag keeps it out of a catalog whose
host doesn't provide it, so headless lists no `gui.*` commands (FR-021,
SC-007).

Host commands run on the emulation thread like any other command. A command
that touches Qt widgets is marshalled to the GUI thread by the host and
awaited; that marshalling belongs to feature 003. The `IHost` contract only
requires that a handler returns its result or a structured error.

Until feature 003 exists, a test `FakeHost` registers a few `gui.*` commands
and proves the catalog, schema and dispatch paths.

## R8. Command naming

Commands use `namespace.verb_object`, for example `core.run_frames`,
`core.read_memory`, `session.save_slot` and `gui.set_fullscreen`. MCP tool
names are identical. 2025-11-25 tool-name guidance allows `A-Z a-z 0-9 _ - .`
in names of 1-128 characters. Namespaces:

| Namespace | Contents |
|---|---|
| `core.` | 1:1 with public `cutenes::Machine` operations (FR-022) |
| `session.` | Consumer-side services: ROM library and uploads, named save slots, captures, audio ring, status |
| `api.` | Surface metadata: version, command catalog, events |
| `gui.` | Host (GUI) actions, GUI mode only |

## R9. Versioning (FR-010)

The surface version is `MAJOR.MINOR`, reported by `api.version` and by MCP
`serverInfo`. Adding a command or an optional field is MINOR. Removing,
renaming, or changing a field's type or meaning is MAJOR. A committed golden
catalog (`tests/api/catalog.golden.json`, names and schemas only, with no
emulator output) makes any catalog change visible in review. A schema diff
without a version bump fails the test.

## R10. Dependencies (constitution: minimal, open source, MIT-compatible)

| Dependency | Licence | Use | How |
|---|---|---|---|
| nlohmann/json | MIT | JSON values and parsing | FetchContent, pinned tag |
| cpp-httplib | MIT | HTTP server and client (API, Streamable HTTP, `--attach`) | FetchContent, pinned tag |
| stb_image_write | Public domain / MIT | PNG encoding | vendored single header, `third_party/NOTICE.md` |

None of these enters `cutenes-core` (constitution II). The exact versions are
pinned at implementation time, at releases at least two weeks old.

## R11. Security posture

- Headless stdio has no listener at all. That is the default (FR-009).
- HTTP listeners bind to loopback only unless explicitly told otherwise, and
  validate `Origin`. Requests with a non-loopback `Host` are rejected.
- ROM library paths are resolved inside the configured library root, and `..`
  or an absolute path is rejected (spec edge case).
- **Per-launch token** (FR-024, maintainer decision 2026-10-29). Each HTTP
  listener generates 32 random bytes from the OS CSPRNG (`std::random_device`
  is not guaranteed cryptographic, so use the platform API:
  `getrandom`/`arc4random_buf`/`BCryptGenRandom`, behind one small
  interface). The token is sent as `Authorization: Bearer <hex>` and compared
  in constant time. A missing or wrong token gets 401 with no session effect.
  - Token file: `<user state dir>/cutenes/<listener>.token` (Linux
    `$XDG_STATE_HOME`, macOS `~/Library/Application Support`, Windows
    `%LOCALAPPDATA%`), created with owner-only permissions (0600 /
    owner-only ACL), replaced on each start and removed on clean exit.
  - The file holds the URL and the token, so `--attach` and scripts need one
    read.
  - The token is never logged. stdio needs no token.
  - Why a token and not OAuth: MCP's authorization spec targets remote
    servers with an authorization server. A local single-user tool needs only
    to keep other local accounts out, which a secret readable only by the
    operator achieves.
