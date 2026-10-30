# Feature Specification: AI MCP Server

**Feature Branch**: `002-ai-mcp-server`

**Created**: 2026-10-17

**Status**: Draft (amended 2026-10-29: run modes, public API, GUI control)

**Input**: User description: "ai mcp server"

## Clarifications

### Session 2026-10-29

- Q: Which surfaces exist? → A: Two layers. A **public API** (versioned,
  documented, usable directly by scripts and tools) has full control over the
  core. The **MCP server** is a protocol adapter over the same API, so MCP and
  the API can never diverge.
- Q: How is the emulator run? → A: In two modes, both required. **Headless
  mode**: a standalone executable with no GUI, where the API and MCP control
  the core. **GUI mode**: the Qt application (feature 003, #32) hosts the
  session, and the API and MCP control the core and every GUI action.
- Q: What does "full control over the GUI" cover? → A: Every GUI action. Each
  menu action, setting, window and debug view is a command the API (and so
  MCP) can call; nothing is GUI-only (constitution Principle V).
- Q: How is headless mode enabled, given that servers are off by default? →
  A: Launching the headless executable is the operator's opt-in. An MCP
  client starts it over stdio, and it opens no network listener unless a
  command-line flag asks for one. The GUI's off-by-default toggles apply to
  GUI mode only.
- Q: The GUI does not exist yet; what is built now? → A: The API, headless
  MCP, and the extension point through which a host (the GUI) registers its
  commands, tested with a fake host. The GUI-mode transport and the GUI's
  commands land with feature 003.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - AI Client Connects and Discovers Capabilities (Priority: P1)

As an AI agent (or its operator), I need to register cuteNES as a standard
MCP server in a mainstream MCP client, connect, and receive a complete,
schema-described catalog of the emulator's automation capabilities — with no
custom client software — so that any compliant AI can immediately see what it
can do with the emulator.

**Why this priority**: Connection and discovery are the entry point; no other
capability matters if standard clients cannot attach and enumerate the tool
surface. This slice alone is a viable MVP: an operator can connect a client
and explore the emulator's capabilities.

**Independent Test**: Register the server in a mainstream MCP client using
standard configuration only, connect, and issue the protocol's discovery
request; verify the complete tool catalog with valid schemas is returned.

**Acceptance Scenarios**:

1. **Given** a mainstream MCP client with only standard server registration,
   **When** it connects to the running emulator, **Then** the connection
   succeeds and discovery returns the full catalog of automation capabilities
   with machine-readable schemas.
2. **Given** a connected client, **When** it requests a capability the server
   does not offer, **Then** the server responds with a structured protocol
   error rather than failing silently.

---

### User Story 2 - AI Plays a Game End-to-End (Priority: P2)

As an AI agent, I need the complete play loop: load a game, observe the screen,
read machine state, inject controller input, and advance execution frame by
frame — so that I can actually play NES games and pursue objectives through
the same information a human player has (and more).

**Why this priority**: This is the project's differentiating value: an AI that
can see, decide, and act on a real game. It builds on P1's connection and
exercises the emulation core end to end.

**Independent Test**: Script an agent (no human input) to boot a known game,
navigate the title/start menu using screen captures and memory reads, and
reach active gameplay.

**Acceptance Scenarios**:

1. **Given** a connected client and a valid ROM, **When** the client loads the
   ROM and advances execution, **Then** the game boots and per-frame screen
   captures reflect the game state.
2. **Given** a game at its title screen, **When** the client injects the
   correct button sequence via input tools and advances frames between
   presses, **Then** the game responds exactly as it does to the equivalent
   physical input and active gameplay is reached.
3. **Given** any point in a running game, **When** the client reads memory and
   CPU state, **Then** values are consistent with the same instant of
   emulation shown in the screen capture.

---

### User Story 3 - Deterministic Branching with Save States (Priority: P3)

As an AI agent or experimenter, I need to capture the machine state, continue
one branch of play, then restore and replay another branch with byte-identical
results — so that I can explore alternatives, verify hypotheses, and retry
from checkpoints deterministically.

**Why this priority**: Deterministic branch-and-retry is what turns AI play
into AI experimentation and verification; it depends on P2's play loop being
reliable first.

**Independent Test**: Capture a state at a chosen frame, run 500 frames
recording outputs, restore, and rerun — outputs must be byte-identical across
at least 10 save/restore cycles.

**Acceptance Scenarios**:

1. **Given** a running game, **When** the client saves state to a named slot,
   runs, then restores that slot, **Then** subsequent outputs are identical to
   the original run from that point.
2. **Given** multiple named save slots, **When** slots are saved, listed, and
   restored in any order, **Then** each restore produces the exact behavior
   captured at save time.

---

### User Story 4 - Machine State Observation and Modification (Priority: P4)

As an AI agent or tool developer, I need to read and (explicitly) write
emulated memory and CPU state through the same connection I use for play —
so that I can reason about game internals, build trainer/cheat-style logic,
and debug without a separate tool.

**Why this priority**: Beyond-pixel observation is a major advantage AI
automation has over screen-only play; it rounds out the tool surface after
the core loop exists.

**Independent Test**: Read a known memory location that changes with
observable game state (e.g., a score counter), verify it matches the on-screen
value; write a location and observe the corresponding in-game effect.

**Acceptance Scenarios**:

1. **Given** a running game with a visible score, **When** the client reads
   the score's memory location, **Then** the value matches the displayed
   score at that frame.
2. **Given** a writable memory location, **When** the client writes a value,
   **Then** subsequent reads return that value and, where the game consumes
   it, the in-game effect is visible in the next screen capture.

---

### User Story 5 - Safe Coexistence with Human Play (Priority: P5)

As a human player using the graphical frontend, I need the MCP server to
coexist safely: I control whether it is active, my game session is never
corrupted by automation, and every state change from either surface is
immediately consistent on both — so that AI assistance never sabotages a play
session.

**Why this priority**: Automation parity is constitutional, but trust requires
that sharing the emulator with AI clients be safe and predictable; it
matters most once the core loop (P1-P4) works.

**Independent Test**: While a human-driven session is in progress, connect an
MCP client, interleave state-changing operations from both surfaces, and
verify neither surface ever observes a torn or inconsistent machine state.

**Acceptance Scenarios**:

1. **Given** a freshly installed emulator, **When** it starts, **Then** both
   the API server and the MCP server are disabled, nothing listens, and the
   emulator behaves exactly as if the features did not exist.
2. **Given** the application GUI, **When** the operator opens the server
   settings, **Then** both servers show as off with independent manual
   toggles, and the MCP toggle cannot be turned on (or the MCP server
   installed) unless the API server is enabled first.
3. **Given** simultaneous use of the graphical frontend and a connected MCP
   client, **When** either surface changes state (load, reset, save, input),
   **Then** both surfaces reflect the same consistent machine state
   afterward.
4. **Given** a connected MCP client, **When** the operator disables the API
   server in the GUI, **Then** the client receives a structured shutdown
   and the session ends without affecting emulation.
5. **Given** the default configuration, **When** a connection attempt arrives
   from outside the local machine, **Then** it is refused unless the operator
   has explicitly enabled network exposure.

### Edge Cases

- What happens when an MCP client disconnects mid-operation or during
  emulation? (Emulation continues unaffected; reconnect yields consistent
  current state.)
- What happens when a tool is called with invalid arguments, or a tool
  requiring a loaded ROM is called with none? (Structured, machine-readable
  error naming the precondition; no crash, no state change.)
- What happens when multiple MCP clients connect simultaneously? (All share
  the single emulation session; commands serialize in arrival order — see
  Assumptions.)
- What happens when a client and the server speak different protocol
  versions? (Negotiate per the protocol standard or fail with a clear,
  structured error.)
- What happens on very large memory reads or writes? (Bounded, chunked
  transfers rather than unbounded payloads.)
- What happens if a save-state slot name is reused, empty, or invalid?
  (Defined overwrite/rejection behavior with explicit errors.)
- What happens when network exposure is enabled — who may connect? (Explicit
  opt-in only; treated as a trusted context; see Assumptions.)
- What happens when a ROM path contains `..` or an absolute path? (Rejected
  with a structured error; loads stay inside the configured library.)
- What happens when a chunked upload stalls or the client disconnects
  mid-upload? (Partial upload is discarded after a timeout; no partial ROM
  is ever loadable.)
- What happens when an input script is shorter or longer than the requested
  run? (Final entry held past the script's end; extra entries ignored.)
- What happens when a CPU-state write lands mid-frame? (Registers apply
  immediately; interrupt recognition is re-evaluated at the next instruction
  boundary.)
- What happens when an imported raw state blob targets a different cartridge
  or core version? (Rejected with the same structured errors as slot loads.)
- What happens when the operator disables the API while MCP clients are
  connected? (Structured shutdown to each client; emulation unaffected;
  MCP endpoint closes with it — FR-020.)
- What happens on a fresh install with no GUI interaction? (Both servers
  stay disabled; nothing listens; installing or registering the MCP server
  anywhere requires enabling the API first.)
- What happens when the GUI is not yet available (feature 003 pending)?
  (Headless mode is the complete surface; GUI-mode commands do not exist
  and are not listed.)
- What happens when a GUI command is called in headless mode? (It is not
  listed; calling it by name returns a structured "unknown command" error.)
- What happens when the GUI user and an API client act at the same time?
  (Both go through the session's single command queue; see FR-007.)

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The system MUST provide an MCP server conforming to the
  published Model Context Protocol specification, discoverable and drivable
  by mainstream MCP clients without client-side customization.
- **FR-002**: The server MUST expose the complete automation surface: load
  and eject ROMs (by library path or chunked byte upload — FR-014); run
  control (run for N frames, run to a target frame, pause, resume, reset,
  power cycle, frame advance, single-step one CPU instruction); controller
  input injection for both ports (immediate and per-frame scripted —
  FR-019); save/load/list/delete named save-state slots plus raw state
  blob export/import (FR-017); memory read and write; CPU state read AND
  write; side-effect-free PPU debug access (registers, VRAM, OAM,
  palette — FR-015); battery SRAM read/write; audio buffer capture
  (FR-016); screen capture; and a status report tool (FR-018).
- **FR-003**: Every capability of the emulation core MUST be exposed through
  the MCP surface; no core capability may be available only through another
  consumer (automation parity). The audit table in the core contract
  (`specs/001-emulation-core/contracts/core-api.md`) is the checklist.
- **FR-004**: Each exposed capability MUST be described by a machine-readable
  schema (inputs, outputs, errors) delivered through the protocol's discovery
  mechanism.
- **FR-005**: Tool invocations MUST return structured, machine-readable
  errors — including precondition failures (e.g., no ROM loaded), invalid
  arguments, and unsupported operations — and MUST never crash the emulator.
- **FR-006**: Identical sequences of MCP operations against identical starting
  state MUST produce identical results, preserving the core's determinism
  across the protocol boundary.
- **FR-007**: State-changing operations from all attached consumers (MCP
  clients, graphical frontend) MUST be serialized into a single consistent
  order; no consumer may observe a partially applied state change.
- **FR-008**: The server MUST accept local connections by default and MUST
  refuse non-local connections until the operator explicitly enables network
  exposure.
- **FR-009**: In GUI mode the application MUST ship with both the API server
  and the MCP server **disabled by default**. Enabling or disabling either is
  a manual action performed in the application GUI, and the setting persists
  across restarts. Disabled servers MUST have no observable effect on the
  emulator (no listeners, no sockets, no background work). In headless mode
  (FR-021) launching the executable is the opt-in. It serves MCP over stdio
  by default, and it opens a network listener only when a command-line flag
  explicitly requests one.
- **FR-020**: The MCP server MUST depend on the API server: the MCP
  endpoint cannot be enabled, installed, or registered with a client while
  the API server is disabled. In GUI mode the GUI MUST present this
  dependency and offer to enable the API as part of MCP setup. (In headless
  mode the MCP adapter always runs on an in-process API.) Disabling the API while
  MCP clients are connected MUST terminate those sessions gracefully
  (structured shutdown messages; emulation continues unaffected).
- **FR-010**: The tool surface MUST carry an explicit version identifier, and
  incompatible changes to any tool schema MUST increment that version, per the
  constitution's versioned-API requirement.
- **FR-011**: Screen capture results MUST be delivered in a standard image
  format that mainstream MCP clients can display or process.
- **FR-012**: Memory and state access MUST enforce bounded transfer sizes,
  chunking large accesses rather than producing unbounded payloads.
- **FR-013**: The server MUST survive client connect/disconnect cycles at any
  time without altering or interrupting emulation state.
- **FR-014**: ROM loading MUST support both (a) a path relative to the
  operator-configured ROM library — rejecting any path that escapes that
  directory — and (b) chunked byte upload for clients without filesystem
  access, with bounded chunk sizes and an explicit commit/abort that
  discards partial uploads.
- **FR-015**: The server MUST expose side-effect-free reads of PPU state —
  registers, VRAM (with mirroring resolved), OAM, and palette — plus
  corresponding writes, so clients can build debugger views and repair
  video state without perturbing emulation via the $2006/$2007 dance.
- **FR-016**: The server MUST expose audio capture: samples buffered since
  a client-supplied marker (frame index or master cycle), delivered in
  bounded chunks, closing the parity loop with the core's audio capability.
- **FR-017**: Raw save-state blobs MUST be exportable (server → client) and
  importable (client → server, subject to the same validation and rejection
  rules as slot loads), so clients can manage, diff, and archive states
  externally.
- **FR-018**: A status tool MUST report cartridge info (format, mapper,
  sizes, battery, region), run state, frame and master-cycle counters, and
  the core and tool-surface versions.
- **FR-021**: The system MUST support two run modes with the same API and
  tool surface:
  (a) **headless**: a standalone executable with no GUI or Qt dependency,
  serving MCP over stdio and, on explicit flags, the API and MCP over local
  HTTP;
  (b) **GUI**: the Qt application hosts the session and serves the API and
  MCP over local HTTP when enabled, with a stdio bridge so stdio-only MCP
  clients can attach to the running application.
  Both modes MUST expose every core command. GUI mode additionally exposes
  every GUI command (FR-023). A tool that needs the GUI MUST NOT be listed in
  headless mode.
- **FR-022**: The API MUST be public: a versioned, schema-documented local
  protocol that scripts and tools can call directly, with full control over
  the core. Every public operation of `cutenes::Machine` MUST map to an API
  command (the core contract's capability table is the checklist). MCP tools
  MUST be generated from the same command registry, so the two surfaces can't
  diverge.
- **FR-023**: The API MUST provide an extension point through which the host
  registers its own commands, with schemas, in the same registry. The GUI
  MUST register every user-facing action through it (menus, settings,
  windows, debug views, input mapping, speed and fast-forward). In GUI mode
  the API and MCP MUST be able to drive the GUI as fully as a user can.
- **FR-019**: The run tools MUST accept an optional per-frame input script
  for the run's duration: each frame's controller states apply before that
  frame executes; beyond the script's length the final entry is held;
  extra entries are ignored. Scripted runs MUST be deterministic and
  byte-identical to equivalent per-frame tool calls.

### Key Entities

- **MCP Session**: A single client connection through which tool requests
  flow; identifiable, and separable from the emulation itself.
- **Tool**: One exposed automation capability — name, input/output schema,
  error definitions, and version.
- **Tool Invocation**: A single request/response pair — arguments, outcome or
  structured error, and the emulation instant it applied to.
- **Emulation Session**: The single running emulator instance all consumers
  share; the subject of every tool operation.
- **Save-State Slot**: A named, addressable snapshot within the emulation
  session; listable, loadable, deletable.
- **Server Configuration**: Operator-controlled settings persisted across
  restarts — two independent manual toggles (API server, MCP server), both
  defaulting to disabled, with MCP gated on the API being enabled
  (FR-009/FR-020); plus transport/exposure scope and port or endpoint. The
  application GUI is the canonical control surface.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: A mainstream MCP client connects and lists the complete tool
  catalog using standard registration only (zero custom client configuration).
- **SC-002**: A scripted agent with no human input completes the reference
  scenario — boot a known game, navigate its start menu via screen captures
  and/or memory reads, and reach active gameplay — successfully in 100% of
  identical deterministic runs.
- **SC-003**: Excluding emulation time itself, 95% of tool invocations
  complete in under 100 milliseconds, keeping the server out of the critical
  path of interactive AI play.
- **SC-004**: An audit of the emulation core's capability list shows 100%
  coverage by exposed MCP tools.
- **SC-005**: In the default configuration, a network scan during acceptance
  testing finds zero remotely accessible endpoints; with exposure enabled,
  only the explicitly configured endpoint is reachable.
- **SC-007**: The headless tool catalog equals the core command set, with
  zero GUI commands. The GUI-mode catalog equals the core set plus every GUI
  action (100% of the actions listed in feature 003's spec). The same call
  gives the same result in both modes.
- **SC-008**: Every API command is reachable through MCP and every MCP tool
  maps to exactly one API command (automated catalog-equality test).
- **SC-006**: In interleaved human-plus-AI sessions, every tested combination
  of concurrent operations from both surfaces leaves both observing the same
  consistent state (100% of scenarios tested).

## Assumptions

- **Two-layer server model**: the API is the control layer (public, FR-022)
  and the MCP server is the protocol surface on top of it. In GUI mode both
  are disabled by default, enabled manually in the GUI, and MCP requires the
  API (FR-009/FR-020). In headless mode launching the executable is the
  opt-in (FR-021).
- This feature depends on the emulation core (`specs/001-emulation-core`) for
  every capability it exposes; the core's public surface is its contract.
- The targeted MCP specification revision is the current published standard at
  implementation time; the protocol's own versioning governs negotiation.
- Transports: the standard local transport is the default; network exposure is
  an explicit opt-in treated as a trusted context (no authentication
  framework is in scope until separately specified).
- One emulation session per server instance: a single game at a time; multiple
  concurrent sessions are a possible future feature.
- Multiple simultaneous MCP clients are permitted and share that single
  session; commands serialize in arrival order, with no per-client isolation.
- The graphical frontend feature does not exist yet. The host extension
  point (FR-023) is built and tested now with a fake host. The GUI's own
  commands and the GUI-mode transport are delivered with feature 003 (#32),
  whose spec MUST list each GUI action's command.
- Tool names, argument shapes, and schema details are finalized during
  planning; the spec fixes the capability set, not the naming.
- **Core timing will still change before 1.0.** Issue #42 replaces the core's
  `$2002` status-read and NMI timing model
  (`specs/001-emulation-core/analysis/STATUS_READ_MODEL.md`) after this
  feature and the GUI are built. Two consequences for this feature:
  - Tests assert through the tool surface (state, memory, errors, schema,
    determinism between two runs) and do not pin exact frame hashes or exact
    cycle counts. Pin a frame only where nothing else can express the check,
    and then only after viewing it, as `docs/test-corpus.md` requires.
  - Save-state blobs are not a stable format before 1.0. Do not commit
    save-state fixtures. Tests create states at run time. The core already
    rejects states from another core version (`IncompatibleCoreVersion`), and
    #42 will raise the format version.
