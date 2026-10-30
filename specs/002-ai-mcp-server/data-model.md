# Data model: API and MCP server (feature 002)

## Entities

| Entity | Owner | Fields | Rules |
|---|---|---|---|
| **Session** | `cutenes-api` | `Machine`, command queue, emulation thread, mode (`headless`/`gui`), library root, last frame (XRGB + hash), audio ring (10 s), slots, blobs, `pausedByAutomation` | Exactly one per process. Only the emulation thread touches `Machine` (R5) |
| **Command** | registry | `name`, `inputSchema`, `outputSchema`, `flags` {`mutating`, `needsRom`, `hostOnly`}, `handler(Session&, json) -> Result` | Names are unique and `namespace.verb_object` (R8). The schema is validated before the handler runs |
| **CommandRegistry** | `cutenes-api` | Ordered map of name to Command; `version` (MAJOR.MINOR) | Iteration order is deterministic (MCP `tools/list`). Adding or removing host commands emits `catalog_changed` |
| **IHost** | host | `registerCommands(Registry&)`, `onSessionEvent(Event)` | `HeadlessHost` registers nothing. The GUI host registers every `gui.*` action (FR-023) |
| **Invocation** | queue | client id (transport + connection), command, args, completion promise, cancel flag | Runs to completion in arrival order. Cancellation is checked at frame boundaries only |
| **SaveSlot** | Session | `name`, state bytes, `frame`, `masterCycle`, `cartridgeId` | In memory, per session. Loading a slot from another cartridge returns `LoadStateError: WrongCartridge` |
| **Blob** | Session | `blobId`, expected size, received bytes, committed, deadline | Uploads, captured states, memory dumps and raw screens. An uncommitted blob expires 60 s after the last chunk and is never usable before `commit` (spec edge case). At most 16 MiB each and 64 MiB in total; the oldest committed blobs are evicted first |
| **ListenerToken** | each HTTP listener | 32 random bytes (hex), token file path | New on every start. The file is owner-only and removed on clean exit. Never logged or persisted in settings (FR-024) |
| **Event** | Session | type, `frame`, `masterCycle`, payload | Fanned out to API SSE subscribers and the host. The `frame` event is rate-limited to 10 per second |
| **ServerConfig** | host | API on/off, MCP on/off, ports, bind address, library root, debug dir, token file path | GUI: persisted settings, off by default (FR-009). Headless: command-line flags only, never a config file |

## State transitions

```
Session.runState:  NeedsRom --load_rom--> Paused --resume--> Running
                   Running --pause | any advance command--> Paused (pausedByAutomation = true for advance)
                   any --eject--> NeedsRom
```

`pausedByAutomation` is cleared by `core.resume` or a GUI resume. It exists
so the GUI can show why play stopped.

## Determinism boundary (FR-006)

Every command's effect on the `Machine` depends only on its arguments and
the session state. Wall-clock time affects only upload and import expiry and
the rate limit on `frame` events. Neither changes emulation, so identical
command sequences give identical results in both modes.
