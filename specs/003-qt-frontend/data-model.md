# Data model: Qt desktop frontend

**Feature**: 003 | Entities from spec "Key Entities"; file formats are in
[contracts/library-layout.md](contracts/library-layout.md).

| Entity | Fields | Rules |
|---|---|---|
| **Library** | `root`, `index` (LibraryEntry list), `recent` (up to 10 paths, newest first) | `root` comes from Settings or `--library`. The subfolders are created on demand. When `root` is missing or read-only, the user is told and saving is refused (US2-5) |
| **LibraryEntry** | `path`, `size`, `mtime`, `identity`, `name`, `mapper`, `submapper`, `region`, `battery`, `supported`, `reason`, `lastPlayed`, `hasSave`, `stateCount` | Unique by `path`. Several entries may share an `identity`. `supported`/`reason` come from a scratch `LoadResult` |
| **GameIdentity** | `sha1` (40 hex), `id8`, `displayName` | `sha1` covers the ROM after the header and trainer (R7). `displayName` is fixed the first time the game gets a file |
| **BatterySave** | `identity`, `bytes`, `dirty`, `lastWrite` | Written when `dirty` and at least 5 s after `lastWrite`, and always on close (FR-012). Guarded by the game's lock file |
| **StateSlot** | `identity`, `slot` 1-10 or `auto`, `state` (core blob), `thumbnail` (PNG), `savedAt`, `coreVersion` | `selectedSlot` is per game and starts at 1. `undoState` is held in memory only, for one level of undo |
| **RewindBuffer** | `budgetBytes` (default 64 MiB), `keyframeInterval` (60), entries (keyframe or delta) | Cleared on load, reset, power cycle and state load. Disabled when the budget is 0 |
| **Movie** | `identity`, `coreVersion`, `region`, `start` (power-on or state), `frames` (2 bytes each), `rerecords`, `events`, `mode` (recording, playing read-only, playing read-write) | Refused for a different `identity`. Taking over from read-only truncates at the current frame and starts recording |
| **Cheat** | `label`, `kind` (freeze, set, game_genie), `address`, `value`, `code`, `enabled` | `freeze` writes each frame before emulation, `set` writes once on enable, and `game_genie` goes to the core (R10) |
| **CheatSearch** | `snapshot` (CPU RAM `$0000-$07FF` plus PRG-RAM), `candidates` (address set) | Resets to all addresses. Each filter compares with the previous snapshot or a value |
| **Breakpoint** | `address`, `enabled` | Execution breakpoints only (spec assumption). Any number |
| **InputBinding** | `port` 1-2, `button` (A, B, Select, Start, Up, Down, Left, Right, TurboA, TurboB), `device` (keyboard or a gamepad GUID), `control` | Conflicts are reported (FR-022). A gamepad binding follows the device's GUID, so a re-plugged pad keeps its port |
| **Hotkey** | `action` (a `gui.*` command plus fixed arguments), `bindings` (up to two: key combination or gamepad button/combination) | Every menu action can be rebound, cleared or reset to its default (contract "Remapping shortcuts"). One binding cannot serve two actions |
| **Settings** | groups `video`, `audio`, `input`, `hotkeys`, `emulation`, `library`, `servers` | Persisted in INI (R8). Every key has a schema, and `gui.set_setting` validates against it |
| **ServerSettings** | `apiEnabled` (false), `apiPort` (8765), `mcpEnabled` (false), `mcpPort` (8766) | MCP on implies API on, and API off implies MCP off (FR-071, 002 FR-020). Tokens are per launch and never stored in settings |

## Settings keys

| Group | Keys (default) |
|---|---|
| `video` | `scale` (3), `fullscreen` (false), `integerScaling` (true), `aspect` (`8:7`), `overscan` (top 8, bottom 8, left 0, right 0), `filter` (`nearest`), `palette` (`default`), `showFps` (false), `showOsd` (true), `alwaysOnTop` (false), `syncMode` (`auto`: display or audio, R4) |
| `audio` | `device` (`default`), `volume` (100), `muted` (false), `latencyMs` (64), `fastForwardAudio` (`stretch`: or `mute`) |
| `input` | bindings (InputBinding list), `turboRate` (15), `allowOppositeDirections` (false), `pauseOnGamepadDisconnect` (true) |
| `hotkeys` | Hotkey list |
| `emulation` | `pauseWhenInactive` (true), `rewindEnabled` (true), `rewindBudgetMiB` (64), `fastForwardSpeed` (300), `resumeOnOpen` (false), `region` (`auto`) |
| `library` | `root` (`<Documents>/cuteNES`) |
| `servers` | ServerSettings |

## State transitions (window)

```text
Library ──open ROM──▶ Running ◀──resume── Paused
   ▲                    │  │                 ▲
   └──close game────────┘  └──pause / focus lost / pad lost / breakpoint
```

- **Closing a game** writes the battery save, then the auto state when
  `resumeOnOpen` is on, then releases the lock.
- **Exit** does the same and then shuts the session down. Automation calls
  in progress get `ShuttingDown`.
