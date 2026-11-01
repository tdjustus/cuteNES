# Contract: Command catalog

**Feature**: 002 | **Authority**: spec FR-002, FR-003, FR-015-FR-023

Every API method and every MCP tool is one entry in this catalog (research
R1). Argument and result fields are the contract. Exact JSON Schemas live in
code and are frozen by `tests/api/catalog.golden.json` (research R9).

Conventions:
- Binary data is base64 (`bytes`). Every read is capped at 64 KiB and chunked
  with `offset`/`length` (FR-012). Larger data (uploads, save states, memory
  dumps, raw screens) moves through **blob handles** (`blobId`, see
  `session.blob_*`).
- `buttons` is an array of names from `A B Select Start Up Down Left Right`.
- Commands flagged **needs ROM** fail with `NoCartridge` when nothing is
  loaded.
- Every result includes `frame` and `masterCycle`: the emulation instant the
  command saw or left (spec "Tool Invocation"). The registry adds both to
  every output schema, host commands included.

## `core.*`: one command per public `Machine` operation (FR-022)

| Command | Arguments | Result | Machine call | Notes |
|---|---|---|---|---|
| `core.load_rom` | `path` (library-relative) **or** `blobId` (a committed upload); `powerOn` (default true) | `cartridge` info | `loadCartridge`, then `powerOn` | Errors: `LoadError` name + `detail`; `PathOutsideLibrary`; `NotFound`. The ROM is checked on a scratch machine first, so a rejected ROM leaves the loaded game in place |
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
| `core.peek` | `addr`, `length` | `bytes` | `peek` per byte | No side effects. The register space (`$2000-$5FFF`) reads as placeholders; observe registers with `core.read_ppu_registers` |
| `core.write_memory` | `addr`, `bytes` | none | `writeMemory` | Errors: `WriteError` name |
| `core.write_cpu_register` | `addr`, `value` | none | `writeCpuRegister` | |
| `core.get_cpu` | none | `a x y s p pc masterCycle` | `cpu` | |
| `core.set_cpu` | any of `a x y s p pc` | `cpu` | `writeCpu` (mask from present fields) | Also covers `debugSetProgramCounter` |
| `core.disassemble` | `addr`, `count` (1-256) | `lines[]` | `disassemble` | No execution |
| `core.read_ppu_registers` | none | 8 bytes | `readPpuRegisters` | No latch side effects |
| `core.read_vram` / `core.write_vram` | `addr`, `length` / `bytes` | `bytes` / none | `readVram` / `writeVram` | Mirroring resolved |
| `core.read_oam` / `core.write_oam` | none / 256 `bytes` | `bytes` / none | `readOam` / `writeOam` | |
| `core.read_palette` / `core.write_palette` | none / 32 `bytes` | `bytes` / none | `readPalette` / `writePalette` | |
| `core.capture_state` | none | `blobId`, `size` | `captureState` | Read the bytes with `session.blob_read` |
| `core.load_state` | `bytes` or `blobId` | none | `loadState` | Errors: `LoadStateError` name |
| `core.read_sram` / `core.load_sram` | `offset`, `length` / `bytes` | `bytes`, `size` / none | `sram` / `loadSram` | needs ROM; `NoBattery` unless the header sets the battery flag |
| `core.region` | none | `region` | `region` | |
| `core.counters` | none | `frame`, `masterCycle` | `frameCounter` / `masterCycle` | |
| `core.trace` | `instructions` (1-256) | `lines[]`: `cpu`, `scanline`, `dot`, `text` (nestest format) | `setInstructionTrace` around `stepInstruction` | Bounded capture (256 keeps a result under 64 KiB); the callback never leaves the emulation thread |
| `core.screen` | `format`: `png` (default) \| `raw` | `png` inline, or `blobId`/`size` (XRGB8888 little-endian); `frameIndex`, `hasFrame`, `frameHash` | last `IVideoSink::onFrame` frame | MCP returns an `image` content block. A PNG is returned whole |
| `core.frame_hash` | none | FNV-1a of the last frame | last frame | Same hash the playtest harness uses |
| `core.audio` | `sinceMasterCycle` (default 0), `maxSamples` (1-32768) | `pcm` bytes (s16le mono), `samples`, `sampleRate`, `startMasterCycle`, `next`, `truncated`, `dropped` | `IAudioSink::onSamples` ring | 10 s ring (R6). Whole core chunks starting at or after `sinceMasterCycle`; pass `next` to continue. 32768 samples keep a call under 64 KiB. `dropped` means samples after `since` left the ring |
| `core.dump_memory` | none | `blobId`, `size` (dump format) | `memoryDump(0)` (P0, #68) | The same contents `dumpMemory` writes, with timestamp 0 so it is deterministic |

`setVideoSink`/`setAudioSink` are not commands. The session owns the sinks,
and `core.screen`/`core.audio` expose what they capture.

## `session.*`: consumer-side services

| Command | Purpose |
|---|---|
| `session.write_dump` | Write `<datetime>.dmp` via `Machine::dumpMemory` into the server's debug directory (`--debug-dir`, default `debug/`; GUI: a setting). Returns the file name. The path is never client-supplied |
| `session.status` | Cartridge info, run state, paused-by-automation, counters, mode (`headless`/`gui`), core version, surface version (FR-018) |
| `session.library_list` | List ROMs under the configured library root (relative paths) |
| `session.blob_begin` / `session.blob_chunk` / `session.blob_commit` | Chunked upload of a ROM or a save state (FR-014, FR-017). `blob_begin(size)` returns a `blobId`. Chunks are at most 64 KiB and arrive in order (`offset` = bytes so far). Uncommitted blobs expire 60 s after the last chunk and are never usable before `commit` |
| `session.blob_read` / `session.blob_delete` | Chunked download (`offset`, `length` up to 64 KiB, `eof`) of any committed blob: captured states, memory dumps, raw screens, uploads. `blob_delete` discards a blob or abandons an upload |
| `session.save_slot` / `session.load_slot` / `session.list_slots` / `session.delete_slot` | Named in-memory save-state slots. Names are `[A-Za-z0-9_-]{1,64}`, and saving to an existing name overwrites it (spec edge case) |

Blobs are held in memory: at most 16 MiB each and 64 MiB in total. A new
blob evicts the oldest committed ones (later reads of those give
`UnknownHandle`), so a client that keeps a state should keep its bytes or a
slot. One handle mechanism replaces the separate upload and state
export/import commands of the first draft: every large transfer works the
same way.

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
| `UnknownHandle` | Expired, evicted or unknown `blobId`, or an unknown slot |
| `NotFound` | A library path that is not a file, or a missing library root |
| `NoBattery` | `core.read_sram`/`core.load_sram` on a cartridge without the battery flag |
| `NotImplemented` | A GUI-mode `gui.*` command whose feature lands in a later phase (003 G034) |
| `Cancelled` | A run interrupted at a frame boundary (disconnect or GUI stop) |
| `ShuttingDown` | The API was disabled while the call was queued (FR-020) |
| `InternalError` | A handler fault, or (with output validation on, as in the tests) a result that breaks its output schema |
