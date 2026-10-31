# Quickstart and validation scenarios: API and MCP (feature 002)

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build -j8 -LE known-failing      # includes the api/mcp labels
```

This produces `build/server/cutenes-server`.

## Headless MCP in a client (stdio)

Register the command in any MCP client's standard server config:

```json
{ "mcpServers": { "cutenes": {
    "command": "/path/to/cutenes-server",
    "args": ["--library", "/path/to/roms"] } } }
```

In Claude Code: `claude mcp add cutenes -- /path/to/cutenes-server --library /path/to/roms`.

### Client verification (A061, SC-001)

| Client | Version | Date | Transport | Result |
|---|---|---|---|---|
| Claude Code | 2.1.308 | 2026-10-30 | stdio | Pass. Listed all 54 headless tools (2 `api.*`, 40 `core.*`, 12 `session.*`, no `gui.*`). Loaded `instr_test-v5/rom_singles/01-basics.nes` (mapper 0), ran 600 frames, and read `$6000` = `$00` with the `DE B0 61` signature and the text "01-basics … Passed" through `core.peek`. Received a 256x240 PNG image block from `core.screen` |
| Second client | | | | Open: #79 |

Run with a temporary config, so the user's own config was not changed:
`claude -p --strict-mcp-config --mcp-config mcp.json --allowedTools mcp__cutenes "<the steps above>"`.

## Headless API for scripts

```bash
cutenes-server --library ~/roms --api-port 8765 --token-file ./api.token &
TOKEN=$(jq -r .token api.token)
curl -s -H "Authorization: Bearer $TOKEN" localhost:8765/api/v1/rpc \
  -d '{"jsonrpc":"2.0","id":1,"method":"core.load_rom","params":{"path":"game.nes"}}'
curl -s -H "Authorization: Bearer $TOKEN" localhost:8765/api/v1/rpc \
  -d '{"jsonrpc":"2.0","id":2,"method":"core.run_frames","params":{"frames":120}}'
```

## Validation scenarios (each is an automated test)

All twelve scenarios pass: `ctest -L "api|mcp"` on every CI platform (A062).

| # | Scenario | Spec | Test |
|---|---|---|---|
| Q1 | Discovery: `server/discover`, then `tools/list`, returns the full headless catalog with valid schemas. `initialize` (2025-11-25) also works | US1, FR-001, SC-001 | `mcp-discovery` (stdio, both revisions) |
| Q2 | Catalog equality: the API `api.catalog` names equal the MCP `tools/list` names. The headless catalog has no `gui.*`. Every row of the core contract's capability table maps to a `core.*` command | SC-004, SC-007, SC-008 | `api-parity` |
| Q3 | Play loop: load a vendored test ROM, run it, read `$6000` (blargg protocol) and the screen, and see the documented verdict | US2, SC-002 | `mcp-playloop` (`instr_test-v5` official_only) |
| Q4 | Branching: save a slot, run 500 frames recording hashes, load the slot and rerun. Hashes are identical over 10 cycles | US3 | `api-branching` |
| Q5 | Memory: a write to RAM reads back. `peek` of `$2002` doesn't clear VBlank, while `read_memory` does (the flag is observed through `read_ppu_registers`) | US4 | `api-memory` |
| Q6 | Errors: unknown command, bad arguments, no ROM, path escape and over-limit each return the named structured error and change no state | FR-005, edge cases | `api-errors` |
| Q7 | Determinism across surfaces: the same command script over in-process, API HTTP and MCP stdio gives identical results and hashes | FR-006 | `api-determinism` |
| Q8 | Serialization: two clients interleave mutating calls. Each result is consistent with one total order, and none sees a torn state | FR-007, SC-006 | `api-concurrency` |
| Q9 | Host extension: with `FakeHost`, `gui.*` commands appear in both catalogs and dispatch. A `catalog_changed` event and an MCP list-changed notification are emitted | FR-023, SC-007 | `api-host` |
| Q10 | Security: no listener with default flags; HTTP rejects a missing or wrong token (401), a non-loopback `Host` and a foreign `Origin`; the token file is owner-only and the token never appears in logs; `--bind` is needed to listen elsewhere | FR-008, FR-009, FR-024, SC-005 | `server-security` |
| Q11 | Disconnect and cancel: a client drops mid-`run_frames`. The run stops at a frame boundary and the session still works for the next client | FR-013 | `mcp-disconnect` |
| Q12 | Latency: 95% of non-emulating calls (status, peek, get_cpu, catalog) finish in under 100 ms over HTTP | SC-003 | `api-latency` (Release only) |

Tests pin no frame hashes from commercial games. Hashes are compared between
two runs in the same test (spec Assumptions: core timing changes before 1.0).
Q3 asserts the ROM's own documented verdict.
