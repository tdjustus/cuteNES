# Contract: Command catalog

**Feature**: 002 | **Authority**: spec FR-002, FR-003, FR-015-FR-023

Every API method and every MCP tool is one entry in this catalog (research
R1). Argument and result fields are the contract. Exact JSON Schemas live in
code and are frozen by `tests/api/catalog.golden.json` (research R9).

Conventions:
- Binary data is base64 (`bytes`). Every read is capped at 64 KiB and chunked
  with `offset`/`length` (FR-012).
- `buttons` is an array of names from `A B Select Start Up Down Left Right`.
- Commands flagged **needs ROM** fail with `NoCartridge` when nothing is
  loaded.
- Every result includes `frame` and `masterCycle`: the emulation instant the
  command saw or left (spec "Tool Invocation").

## `core.*`: one command per public `Machine` operation (FR-022)

| Command | Arguments | Result | Machine call | Notes |
|---|---|---|---|---|
| `core.load_rom` | `path` (library-relative) **or** `uploadId` | `cartridge` info | `loadCartridge` | Errors: `LoadError` name + `detail`; `PathOutsideLibrary` |
| `core.eject` | none | none | `ejectCartridge` | |
| `core.cartridge_info` | none | format, mapper, mirroring, battery, sizes, region | `cartridgeInfo` | needs ROM |
| `core.set_mmc3_revision` | `revision`: `auto` \| `A` \| `B` | none | `setMmc3Revision` | |
| `core.get_mmc3_revision` | none | `revision` | `mmc3Revision` | |
| `core.power_on` | none | none | `powerOn` | needs ROM |
| `core.reset` | none | none | `reset` | needs ROM |
| `core.run_frames` | `frames` (1-216000; default cap 3600), optional `script` (array of per-frame `buttons`, held past the end) | `frames` run, `frameHash` | `runFrames(n[, script])` | needs ROM; pauses autonomous pacing (R5) |
| `core.frame_advance` | none | `frameHash` | `frameAdvance` | needs ROM |
| `core.step_instruction` | `count` (1-100000, default 1) | `cpu` | `stepInstruction` | needs ROM |
| `core.pause` / `core.resume` | none | `runState` | `pause` / `resume` | Autonomous pacing (GUI) |
| `core.run_state` | none | `runState` | `runState` | |
| `core.set_controller` | `port` 1\|2, `buttons` | none | `setController` | |
| `core.get_controller` | `port` | `buttons` | `controller` | |
| `core.read_memory` | `addr`, `length` | `bytes` | `readMemory` | Read side effects apply (e.g. `$2002`) |
| `core.peek` | `addr`, `length` | `bytes` | `peek` per byte | No side effects |
| `core.write_memory` | `addr`, `bytes` | none | `writeMemory` | Errors: `WriteError` name |
| `core.write_cpu_register` | `addr`, `value` | none | `writeCpuRegister` | |
| `core.get_cpu` | none | `a x y s p pc masterCycle` | `cpu` | |
| `core.set_cpu` | any of `a x y s p pc` | `cpu` | `writeCpu` (mask from present fields) | Also covers `debugSetProgramCounter` |
| `core.disassemble` | `addr`, `count` (1-256) | `lines[]` | `disassemble` | No execution |
| `core.read_ppu_registers` | none | 8 bytes | `readPpuRegisters` | No latch side effects |
| `core.read_vram` / `core.write_vram` | `addr`, `length` / `bytes` | `bytes` / none | `readVram` / `writeVram` | Mirroring resolved |
| `core.read_oam` / `core.write_oam` | none / 256 `bytes` | `bytes` / none | `readOam` / `writeOam` | |
| `core.read_palette` / `core.write_palette` | none / 32 `bytes` | `bytes` / none | `readPalette` / `writePalette` | |
| `core.capture_state` | none | `bytes` (chunked; see `session.state_export`) | `captureState` | |
| `core.load_state` | `bytes` or `stateId` | none | `loadState` | Errors: `LoadStateError` name |
| `core.read_sram` / `core.load_sram` | `offset`, `length` / `bytes` | `bytes` / none | `sram` / `loadSram` | needs battery cartridge |
| `core.region` | none | `region` | `region` | |
| `core.counters` | none | `frame`, `masterCycle` | `frameCounter` / `masterCycle` | |
| `core.trace` | `instructions` (1-100000) | `lines[]` (nestest format) | `setInstructionTrace` around `stepInstruction` | Bounded capture; the callback never leaves the emulation thread |
| `core.screen` | `format`: `png` (default) \| `raw` | `png` or `bytes` (XRGB8888) | last `IVideoSink::onFrame` frame | MCP returns an `image` content block |
| `core.frame_hash` | none | FNV-1a of the last frame | last frame | Same hash the playtest harness uses |
| `core.audio` | `since`: `{ frame }` \| `{ masterCycle }`, `maxSamples` | `pcm` bytes (s16 mono), `sampleRate`, `startMasterCycle`, `truncated` | `IAudioSink::onSamples` ring | 10 s ring, 1 s per call (R6) |
| `core.dump_memory` | none | `bytes` (dump format, chunked like state export) | `memoryDump` (P0, #68) | The same contents `dumpMemory` writes |

`setVideoSink`/`setAudioSink` are not commands. The session owns the sinks,
and `core.screen`/`core.audio` expose what they capture.

## `session.*`: consumer-side services

| Command | Purpose |
|---|---|
| `session.write_dump` | Write `<datetime>.dmp` via `Machine::dumpMemory` into the server's debug directory (`--debug-dir`, default `debug/`; GUI: a setting). Returns the file name. The path is never client-supplied |
| `session.status` | Cartridge info, run state, paused-by-automation, counters, mode (`headless`/`gui`), core version, surface version (FR-018) |
| `session.library_list` | List ROMs under the configured library root (relative paths) |
| `session.upload_begin` / `session.upload_chunk` / `session.upload_commit` / `session.upload_abort` | Chunked ROM upload (FR-014). `upload_begin` returns an `uploadId` handle. Chunks are at most 64 KiB. Uncommitted uploads expire after 60 s |
| `session.save_slot` / `session.load_slot` / `session.list_slots` / `session.delete_slot` | Named in-memory save-state slots. Names are `[A-Za-z0-9_-]{1,64}`, and saving to an existing name overwrites it (spec edge case) |
| `session.state_export` / `session.state_import_begin` / `..._chunk` / `..._commit` | Raw state blob transfer (FR-017), chunked like uploads; import validates through `loadState` |

## `api.*`: surface metadata

| Command | Purpose |
|---|---|
| `api.version` | Surface `MAJOR.MINOR`, core version, mode |
| `api.catalog` | Every command available in this mode with its schemas (the API-side equivalent of `tools/list`) |

## `gui.*`: host commands (GUI mode only, FR-023)

Feature 003 defines the list. Its spec must give every user-facing action a
`gui.*` command (menus, settings, windows, debug viewers, input mapping,
speed, fast-forward, rewind, fullscreen, window screenshot). These commands
are never listed in headless mode (SC-007).

## Errors

All failures are structured: `{ code, name, message, data }`. JSON-RPC uses
codes from `-32000` to `-32019` (implementation-defined; the MCP error-code
policy in 2026-07-28 reserves `-32020` to `-32099` for the spec). MCP tool
failures are returned as a tool result with `isError: true` and the same
`name`/`data` in `structuredContent`.

| `name` | When |
|---|---|
| `UnknownCommand` | Not in this mode's catalog |
| `InvalidArguments` | Fails the input schema; `data.path` names the field |
| `NoCartridge` | needs-ROM command with nothing loaded |
| `LoadError` | `data.error` is the core `LoadError` name, `data.detail` its detail |
| `LoadStateError`, `WriteError` | `data.error` is the core enum name |
| `PathOutsideLibrary` | `..`, an absolute path, or a symlink escaping the root |
| `LimitExceeded` | Over a per-call bound; `data.max` gives it |
| `UnknownHandle` | Expired or unknown `uploadId`/`stateId`/slot |
| `Cancelled` | A run interrupted at a frame boundary (disconnect or GUI stop) |
| `ShuttingDown` | The API was disabled while the call was queued (FR-020) |
