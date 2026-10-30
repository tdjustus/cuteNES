# Implementation plan: API and MCP server

**Branch**: per-phase branches (see tasks.md) | **Date**: 2026-10-29 | **Spec**: [spec.md](spec.md) | **Issue**: #31

**Input**: Feature specification from `specs/002-ai-mcp-server/spec.md`,
amended 2026-10-29 (run modes, public API, GUI control).

## Summary

Add three layers above `cutenes-core`, each consuming only the layer below:

1. **`cutenes-api`**: a C++20 library with a **Session**, which owns the one
   `Machine` and a single command queue, and a **command registry**. The
   registry gives full control over the core (one `core.*` command per public
   `Machine` operation), consumer services (`session.*`), and an **`IHost`
   extension point** through which a host registers its own commands.
2. **API server**: the registry served as JSON-RPC 2.0 over loopback HTTP,
   with an SSE event stream. It is public and versioned, so scripts can call
   it directly.
3. **`cutenes-mcp`**: an MCP adapter that lists the same registry as tools.
   It targets MCP 2026-07-28 and accepts 2025-11-25 clients, over stdio and
   Streamable HTTP.

There are two run modes, sharing one surface. **Headless**:
`cutenes-server`, with no Qt, MCP over stdio by default, and HTTP only on
explicit flags. Its catalog is the core plus session commands. **GUI**: the
Qt app (feature 003) hosts the same Session and registers every GUI action
as `gui.*`. MCP then controls the core and the GUI, and `cutenes-server
--attach` bridges stdio clients into the running app.

Decisions and sources: [research.md](research.md).

## Technical context

**Language/version**: C++20, the same toolchains as the core (MSVC 2022+,
AppleClang 15+, GCC 12+).

**Primary dependencies**: nlohmann/json (MIT), cpp-httplib (MIT),
stb_image_write (public domain / MIT). All are pinned, and none enters
`cutenes-core` (R10).

**Storage**: none on disk. Slots, uploads and the audio ring are in-memory
per session. The GUI persists its server settings (feature 003).

**Testing**: GoogleTest for `cutenes-api` (in-process, the fake host) and
transport tests driving `cutenes-server` over stdio and HTTP. These are
ctest labels `api` and `mcp`, and they run on all three CI platforms. A
schema test validates `tools/list` against the pinned MCP `schema.json`.

**Target platform**: Windows, macOS and Linux (constitution IV). Headless CI
has no display or audio.

**Project type**: two libraries (`cutenes-api`, `cutenes-mcp`) and one
executable (`cutenes-server`). The Qt app (003) links the libraries.

**Performance goals**: under 100 ms at p95 for non-emulating calls (SC-003).
`run_frames` throughput within 5% of a direct `Machine::runFrames` loop, so
the API adds no measurable overhead per frame.

**Constraints**: one thread touches the `Machine` (R5). Determinism is
preserved across transports (FR-006). No listener by default (FR-009). A
bounded payload of 64 KiB per call (FR-012).

**Scale/scope**: about 45 `core.*`/`session.*`/`api.*` commands. One session
per process, with many clients sharing it.

## Constitution check

| Principle | How this plan satisfies it | Verdict |
|---|---|---|
| I. Hardware fidelity | No emulation behavior changes. Tests assert ROM-documented verdicts (Q3), never emulator output | PASS |
| II. Core/UI separation | `cutenes-core` gains no dependency. `cutenes-api` uses only the public core API and has no Qt or MCP types. MCP is a separate library. One prerequisite: the core's `dumpMemory` does file I/O, a contract violation; it is fixed (return bytes) before `core.dump_memory` ships | PASS, with that fix |
| III. Test ROMs are the gate | The full suite still gates every PR. API and MCP tests are added as their own labels | PASS |
| IV. Cross-platform | Every dependency is portable and header-only. `cutenes-server` builds and is tested on the 3-OS matrix | PASS |
| V. Automation parity | One registry gives the API and MCP the same catalog (SC-008). The parity test maps every core capability (SC-004). `IHost` makes every GUI action a command (FR-023) | PASS |
| Constraints: licensing and dependencies | MIT / PD-or-MIT only, recorded in `third_party/NOTICE.md`; three dependencies, each justified in R10 | PASS |
| Release gate "MCP schema validates" | The `mcp-schema` test validates against the pinned official `schema.json` in CI | PASS |

**Post-design re-check.** The contracts add no core types and no GUI types,
and they keep each FR traceable to a command or a quickstart scenario.
**PASS.**

## Project structure

### Documentation

```text
specs/002-ai-mcp-server/
├── spec.md                  # amended 2026-10-29
├── plan.md                  # this file
├── research.md              # R1-R11
├── data-model.md
├── quickstart.md            # Q1-Q12 validation scenarios
├── contracts/
│   ├── command-catalog.md   # every command, args, results, errors
│   └── transports.md        # run modes, API protocol, MCP mapping
└── tasks.md
```

### Source

```text
api/                          # cutenes-api (no Qt, no MCP)
├── include/cutenes/api/      # session.hpp, registry.hpp, host.hpp, command.hpp
└── src/                      # session (queue + emulation thread), core_commands,
                              # session_commands, media (PNG via stb), json_schema
mcp/                          # cutenes-mcp: JSON-RPC, MCP methods, stdio + HTTP
├── include/cutenes/mcp/
└── src/
server/                       # cutenes-server executable (headless, --attach)
└── main.cpp
tests/api/                    # GoogleTest: registry, commands, parity, FakeHost
tests/mcp/                    # stdio/HTTP transport tests, schema validation
third_party/stb/              # stb_image_write.h + licence
```

**Structure decision.** Like `core/`, each layer is its own top-level
directory and CMake target. The dependency direction (`core` <- `api` <-
`mcp` <- `server`/Qt app) is visible in the tree and enforced by link
dependencies.

## Phasing (one branch and PR each, merged before the next)

| Phase | Delivers | Exit |
|---|---|---|
| P0 | Core prerequisites: `dumpMemory` returns bytes (fixes #68), the audit table in core-api.md is completed | Core suite green, contract updated |
| P1 | `cutenes-api`: Session, registry, every `core.*` and `session.*` command, in-process tests, parity test | Q2 (in-process), Q4-Q8 in-process |
| P2 | API server over HTTP + SSE, plus `cutenes-server --api-port` | Q6-Q8 over HTTP, Q10, Q12 |
| P3 | `cutenes-mcp`: stdio + Streamable HTTP, both revisions, schema test | Q1, Q2, Q3, Q11 |
| P4 | `IHost` extension with `FakeHost`, `catalog_changed`, list-changed notifications, `--attach` bridge (tested against a second headless instance with `--mcp-port`) | Q9 |
| P5 | Docs (README usage, versioning policy), and the MCP registration example verified against two mainstream clients | SC-001 |

The GUI-mode host and its `gui.*` commands are part of feature 003 (#32).
That spec must enumerate them.

## Complexity tracking

| Item | Why it is needed | Simpler alternative rejected because |
|---|---|---|
| Two MCP revisions | 2026-07-28 is current, but mainstream clients may still speak 2025-11-25 | One revision would lock out one group of clients (SC-001) |
| An emulation thread plus a queue | The GUI loop, the API and MCP clients share one single-threaded core | A mutex allows interleaving and races for order (R5) |
