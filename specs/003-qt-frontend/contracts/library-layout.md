# Contract: The library folder and its files

**Feature**: 003 | **Authority**: spec FR-010-FR-014, FR-033, FR-043, FR-044, FR-050-FR-052, FR-064; research R6-R8, R14

## Location

- The default is `<Documents>/cuteNES`. That is `QStandardPaths::DocumentsLocation`,
  for example `~/Documents/cuteNES` or `C:\Users\<user>\Documents\cuteNES`.
- The location can be changed in Settings > Library, or for one run with
  `--library DIR` (cli.md).
- The location is stored in the settings, which live outside the library
  (FR-014).

## Layout

```text
cuteNES/
├── roms/            the user's ROMs (*.nes), scanned recursively; any subfolders
├── saves/           battery saves:   <game>.sav  (+ <game>.lock while open), identities.json
├── states/          save states:     <game>/slot-01.state + .png + .json … slot-10
│                                     <game>/auto.state    (resume where left off)
├── screenshots/     <game>/<YYYYMMDD-HHMMSS>.png   (native 256x240, FR-043)
├── movies/          <game>/<name>.cnm
├── recordings/      <game>/<YYYYMMDD-HHMMSS>.wav   (FR-044)
├── cheats/          <game>.json
└── debug/           <YYYYMMDDHHMMSS>.dmp (core dump), trace-<YYYYMMDD-HHMMSS>.log
```

At first launch the application creates the folder and its subfolders, and
puts a short `README.txt` in `roms/`.

The user can also add ROM folders anywhere (setting `library.romFolders`).
They are scanned recursively like `roms/`, but only read: per-game files
for their games still go in this library.

## Game names

- `<game>` is `<display name> [<id8>]`:
  - `<id8>` is the first 8 hex digits (lowercase) of the game identity (R7):
    the SHA-1 of the ROM file after its 16-byte header and any 512-byte
    trainer.
  - `<display name>` is the ROM file's stem when the game first gets a file,
    with `<>:"/\|?*` and control characters replaced by `_`, and trimmed to
    80 characters.
- The full identity is recorded beside each per-game item: in
  `identity.txt` inside a per-game folder (`states/<game>/`), and in the
  folder's `identities.json` (`{"<file name>": "<sha1>"}`) for flat files
  such as `saves/<game>.sav`, whose raw format has no room for it.
- A lookup matches on `[<id8>]` (or `[<id8>-<n>]`) and compares the recorded
  identity. A file with no record, such as a save copied in by hand, matches
  on its `id8` alone. So:
  - a renamed ROM keeps its saves;
  - two ROMs that differ only in their headers share saves;
  - an `id8` collision between different games is told apart by the full
    hash, and the second game's files are named `[<id8>-2]`.

## File formats

| File | Format |
|---|---|
| `.sav` | Raw battery RAM: exactly `sram().size` bytes, the format other emulators use (FR-013). Import replaces the whole save: a short file is padded with zeros, a long one trimmed |
| `.state` | The core's save-state blob (`captureState`), unchanged. It carries the core version, so a state from another version is refused with `IncompatibleCoreVersion` |
| `slot-NN.png` | A 256x240 thumbnail taken when the state was saved |
| `slot-NN.json` | `{"name": …}`: what the menus call the state (by default, the time it was saved) |
| `.cnm` | A movie: see below |
| `cheats/<game>.json` | `{"identity": sha1, "cheats": [{"label", "kind": "freeze"\|"set"\|"game_genie", "address", "value", "code", "enabled"}]}` |
| `.png` | A screenshot: lossless, native resolution, with no filter or scaling |
| `.wav` | PCM 16-bit mono at the session's sample rate (48 kHz) |
| `.dmp` | The core's `memoryDump` layout (001 core-api "Memory dump") |
| `.lock` | `QLockFile`, held while a game is open (R8) |

Every write goes to a temporary file in the same folder and is then renamed
over the old file, so a crash never leaves a half-written file (FR-012).
A save that fails to load is renamed `<name>.bad-<YYYYMMDD-HHMMSS>`. It is
never deleted.

## Movie file (`.cnm`)

```text
CNM1\n                         magic + version
{json header}\n                 one line, UTF-8
<body>                          frameCount × 2 bytes: port-1 ButtonSet, port-2 ButtonSet
```

The header is:
- `identity`: the full SHA-1;
- `core`: the core version string;
- `region`;
- `start`: `"power_on"`, or `"state"` with `state` holding a base64 core
  state;
- `frames` and `rerecords`;
- `events`: a list of `{frame, "reset"|"power"}`.

The bit order of each `ButtonSet` byte is the core's serial order (001
core-api "Input"). Playback refuses a file whose `identity` differs from the
loaded game's.

## Index cache

The cache is `<CacheLocation>/library-index.json`. It is not in the library,
and deleting it only causes a full rescan:
- `version`;
- `root`;
- `entries`: one per ROM, with `path` (relative to `roms/` or absolute for
  recent games), `size`, `mtime`, `identity`, `name`, `mapper`, `submapper`,
  `region`, `battery`, `supported`, `reason` and `lastPlayed`.
