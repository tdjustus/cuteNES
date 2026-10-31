---
description: "Task list for the API and MCP server"
---

# Tasks: API and MCP server

**Input**: `specs/002-ai-mcp-server/`: plan.md, spec.md (amended 2026-10-29),
research.md, data-model.md, contracts/, quickstart.md.

**Tests**: included and written first in every phase. Assertions come from
the spec, the contracts, MCP's published schema, or a test ROM's documented
verdict, never from current output. Tests pin no commercial frame hashes:
they compare two runs inside one test (spec Assumptions).

**Organization**: one phase per PR, in order. Each phase is merged before
the next branch starts (`docs/source-control.md`). Issue #31 tracks the
feature. Each PR uses `Refs #31` until P5, which uses `Fixes #31`.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: can run in parallel (different files, no dependency on incomplete
  tasks)
- **[Story]**: US1-US5 from spec.md

---

## Phase P0: Core prerequisites (branch `fix/core-memory-dump-bytes`)

- [x] A001 File the issue: `Machine::dumpMemory` writes a file from the core, contradicting core-api.md "performs no I/O" (constitution II). Filed as #68; P0's PR uses `Fixes #68`
- [x] A002 Test first, in `tests/unit/test_full_control.cpp`: a new `Machine::memoryDump()` returns the dump bytes, byte-identical to the contents `dumpMemory` writes, and touches no filesystem. Also a regression test that `dumpMemory` still writes `<datetime>.dmp` with the same contents
- [x] A003 Implement `memoryDump()` in `core/src/cutenes/system/machine.cpp`, and reimplement `dumpMemory` on top of it. **`dumpMemory` stays** (maintainer, 2026-10-29: it helps debugging): same signature, same file, not deprecated. Document it in core-api.md's global rules as the core's single explicit debug I/O exception (it runs only when called and never affects emulation), and add `memoryDump` to the contract
- [x] A004 [P] Complete the capability table in `specs/001-emulation-core/contracts/core-api.md`: add `cartridgeInfo`, `setMmc3Revision`/`mmc3Revision`, `controller`, `peek`/`disassemble` (already there), `memoryDump`, `setInstructionTrace` (as a bounded trace command), `debugSetProgramCounter` (covered by `writeCpu`), `frameCounter`/`masterCycle`

## Phase P1: `cutenes-api` library (branch `feature/api-library`)

Setup
- [x] A010 Root `CMakeLists.txt`: `add_subdirectory(api)`. `api/CMakeLists.txt`: target `cutenes-api` linking `cutenes-core` PRIVATE→PUBLIC as needed, nlohmann/json via pinned FetchContent, the same warning flags as the core
- [x] A011 [P] Vendor `third_party/stb/stb_image_write.h` plus its licence. Add NOTICE.md entries for nlohmann/json, cpp-httplib and stb
- [x] A012 [P] `tests/api/CMakeLists.txt` (label `api`), wired into `tests/CMakeLists.txt`

Tests first
- [x] A013 [P] [US1] `tests/api/test_registry.cpp`: unique names; deterministic order; schemas are valid JSON Schema 2020-12 (structural check); `api.catalog` matches the registry; `gui.*` commands are absent without a host (a host registers them; there is no `hostOnly` flag)
- [x] A014 [P] [US2] `tests/api/test_core_commands.cpp`, one test per `core.*` row of command-catalog.md, run on a vendored test ROM. Includes Q3: run `instr_test-v5/official_only` through the commands and read `$6000` = 0 (the blargg protocol verdict)
- [x] A015 [P] [US3] `tests/api/test_branching.cpp` (Q4): 10 save/load cycles with identical hashes; slot rules (name validation, overwrite, `WrongCartridge`)
- [x] A016 [P] [US4] `tests/api/test_memory.cpp` (Q5): `peek` vs `read_memory` side effects at `$2002`; write/read-back; 64 KiB chunking and `LimitExceeded`
- [x] A017 [P] `tests/api/test_errors.cpp` (Q6): every error `name` in the contract, each leaving the state unchanged (state hash before and after)
- [x] A018 [P] `tests/api/test_parity.cpp` (Q2 in-process, SC-004): every capability-table row in core-api.md has its `core.*` command (the table is parsed from the markdown, so a new core row without a command fails)
- [x] A019 [P] `tests/api/test_concurrency.cpp` (Q8): N threads submit interleaved mutating commands. The results are consistent with one total order (each result's `frame`/`masterCycle` is monotonic in completion order) and no result is torn
- [x] A020 [P] `tests/api/test_determinism.cpp` (Q7 in-process): the same script on two Sessions gives identical results

Implementation
- [x] A021 `api/include/cutenes/api/{command.hpp,registry.hpp,session.hpp,host.hpp}`: the types in data-model.md
- [x] A022 Session: emulation thread, FIFO queue, run-to-completion, cancellation at frame boundaries, `pausedByAutomation` (R5)
- [x] A023 JSON Schema subset validator for inputs (type, required, enum, min/max, `maxItems`, pattern); `InvalidArguments` with `data.path`
- [x] A024 `core.*` commands in `api/src/core_commands.cpp`, following command-catalog.md
- [x] A025 Media: last-frame capture, FNV-1a hash, PNG via stb (`core.screen`), 10 s audio ring (`core.audio`)
- [x] A026 `session.*` commands: library (root confinement, symlink check), blob handles (uploads and every large download, replacing the separate upload and state export/import commands; command-catalog.md "Blobs"), slots, status
- [x] A027 `api.version`/`api.catalog`. Write `tests/api/catalog.golden.json` and the test that fails on a schema diff without a version bump (R9)

## Phase P2: API server and headless executable (branch `feature/api-server`)

- [x] A030 [P] Tests first, in `tests/api/test_http.cpp`: JSON-RPC single/batch/notification/parse-error per the JSON-RPC 2.0 spec; the SSE `state_changed` event after `core.load_rom`; Q6-Q8 repeated over HTTP
- [x] A031 [P] Tests first, in `tests/api/test_security.cpp` (Q10): no listener without flags; 401 with no token, a wrong token, or the previous launch's token; 403 on a non-loopback `Host`; 403 on a foreign `Origin`; loopback bind; owner-only token file permissions (POSIX mode check; Windows ACL check); the token absent from stderr and logs
- [x] A032 `api/src/http_server.cpp` (cpp-httplib): `POST /api/v1/rpc`, `GET /api/v1/events`, loopback bind, `Origin`/`Host` checks, bearer-token check (constant time)
- [x] A032a `api/src/token.cpp`: CSPRNG behind one interface (`getrandom`/`arc4random_buf`/`BCryptGenRandom`); token file in the per-user state dir with owner-only permissions, holding the URL and token; replaced on start, removed on clean exit (R11, FR-024)
- [x] A033 `server/main.cpp`: `cutenes-server` with the flags and exit codes in transports.md (headless host, `--library`, `--debug-dir`, `--api-port`, `--bind`, `--token-file`; prints the token file's location, never the token, to stderr)
- [x] A034 `tests/api/test_latency.cpp` (Q12, Release only): p95 under 100 ms for status, peek, get_cpu, catalog
- [x] A035 CI: build `cutenes-server` on the 3-OS matrix and run the `api` label (the server is part of the default build; `api` runs in every job, including the Debug and sanitizer fast groups)

## Phase P3: MCP adapter (branch `feature/mcp-adapter`)

- [x] A040 [P] [US1] Tests first, in `tests/mcp/test_discovery.cpp` (Q1): a stdio subprocess; `server/discover` (2026-07-28) and `initialize` (2025-11-25) each give the catalog; deterministic `tools/list` order; `ttlMs`/`cacheScope`; `UnsupportedProtocolVersion` (-32022)
- [x] A041 [P] `tests/mcp/test_schema.cpp`: validate every `tools/list` result and tool result against the pinned official MCP `schema.json` for both revisions (the release gate "MCP schema validates")
- [x] A042 [P] [US2] `tests/mcp/test_playloop.cpp` (Q3 over MCP): load, run, `core.screen` returns a valid PNG `image` block, and `$6000` shows the verdict
- [x] A043 [P] `tests/mcp/test_parity.cpp` (SC-008): the `tools/list` names equal the `api.catalog` names
- [x] A044 [P] `tests/mcp/test_disconnect.cpp` (Q11): kill the client mid-run; the run stops with `Cancelled` and the next client sees a consistent session
- [x] A045 `mcp/src`: JSON-RPC dispatcher, `server/discover`, `initialize`, `tools/list`, `tools/call`, the error mapping, and the image content for `core.screen`
- [x] A046 stdio transport (stdout is protocol only; logs go to stderr) as the default `cutenes-server` mode
- [x] A047 Streamable HTTP at `POST /mcp` (`--mcp-port`): `Mcp-Method`/`Mcp-Name` headers, a JSON or SSE response, `notifications/progress` on long runs, the same security checks as the API (token, Host, Origin)

## Phase P4: Host extension and the attach bridge (branch `feature/api-host-extension`)

- [ ] A050 [P] [US5] Tests first, in `tests/api/test_host.cpp` (Q9): `FakeHost` registers `gui.test_echo` and `gui.test_toggle`; they appear in `api.catalog` and `tools/list`; they dispatch on the emulation thread; adding or removing them emits `catalog_changed` and an MCP list-changed notification (both revisions)
- [ ] A051 [P] `tests/mcp/test_attach.cpp`: instance A runs `--mcp-port`; instance B runs `--attach` over stdio and finds A through A's token file. A stdio client through B sees A's catalog and state; with A down (no token file), B exits 4 with a message naming the setting; with a stale token, B reports 401 clearly
- [ ] A052 `IHost` registration, event fan-out, and `catalog_changed`
- [ ] A053 `--attach` bridge (cpp-httplib client relaying stdio to Streamable HTTP; reads the URL and token from the token file, or `--token-file`)
- [ ] A054 Add the `gui.*` command requirement to issue #32 and to the feature 003 spec input: every GUI action gets a command, plus the host lifecycle (register at startup, marshal to the GUI thread)

## Phase P5: Docs and client verification (branch `docs/mcp-usage`)

- [ ] A060 README: headless MCP registration, script API examples, the versioning policy, the security defaults
- [ ] A061 Verify registration with two mainstream MCP clients (stdio) and record the client versions in quickstart.md (SC-001)
- [ ] A062 Mark quickstart Q1-Q12 as passing, and close #31 (`Fixes #31`) only when every FR except the GUI-mode items owned by 003 is met. Record the GUI-mode remainder against #32

## Dependencies

P0 → P1 → P2 → P3 → P4 → P5, strictly in order (one branch at a time). Inside
a phase, the tests marked [P] are written first and must fail before the
implementation tasks start.
