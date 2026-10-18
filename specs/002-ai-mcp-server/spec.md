# Feature Specification: AI MCP Server

**Feature Branch**: `002-ai-mcp-server`

**Created**: 2026-10-17

**Status**: Draft

**Input**: User description: "ai mcp server"

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

1. **Given** a locally running emulator, **When** no MCP client is connected
   and the server is disabled, **Then** the emulator behaves exactly as if
   the feature did not exist.
2. **Given** simultaneous use of the graphical frontend and a connected MCP
   client, **When** either surface changes state (load, reset, save, input),
   **Then** both surfaces reflect the same consistent machine state
   afterward.
3. **Given** the default configuration, **When** a connection attempt arrives
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

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The system MUST provide an MCP server conforming to the
  published Model Context Protocol specification, discoverable and drivable
  by mainstream MCP clients without client-side customization.
- **FR-002**: The server MUST expose the complete automation surface: load and
  eject ROMs; run control (run for N frames, pause, resume, reset, frame
  advance, single-step one CPU instruction); controller input injection for
  both controllers; save/load/list/delete named save-state slots; memory read
  and write; CPU state read; and screen capture.
- **FR-003**: Every capability of the emulation core MUST be exposed through
  the MCP surface; no core capability may be available only through another
  consumer (automation parity).
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
- **FR-009**: The operator MUST be able to enable and disable the MCP server,
  and a disabled server MUST have no observable effect on the emulator.
- **FR-010**: The tool surface MUST carry an explicit version identifier, and
  incompatible changes to any tool schema MUST increment that version, per the
  constitution's versioned-API requirement.
- **FR-011**: Screen capture results MUST be delivered in a standard image
  format that mainstream MCP clients can display or process.
- **FR-012**: Memory and state access MUST enforce bounded transfer sizes,
  chunking large accesses rather than producing unbounded payloads.
- **FR-013**: The server MUST survive client connect/disconnect cycles at any
  time without altering or interrupting emulation state.

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
- **Server Configuration**: Operator-controlled settings — enabled/disabled,
  transport/exposure scope, port or endpoint — persisted across restarts.

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
- **SC-006**: In interleaved human-plus-AI sessions, every tested combination
  of concurrent operations from both surfaces leaves both observing the same
  consistent state (100% of scenarios tested).

## Assumptions

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
- The graphical frontend feature does not exist yet; the parity requirement
  (FR-003) applies to the core now and extends to frontend features when that
  feature is specified.
- Tool names, argument shapes, and schema details are finalized during
  planning; the spec fixes the capability set, not the naming.
