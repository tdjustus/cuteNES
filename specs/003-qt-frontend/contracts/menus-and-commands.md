# Contract: Menus, `gui.*` commands and shortcuts

**Feature**: 003 | **Authority**: spec FR-020-FR-024, FR-070-FR-074; research R11

This is the **menu inventory**. Every row is one `QAction`, and its `gui.*`
command performs it. The `gui-audit` test (SC-003) checks that:
- the set of menu actions equals this table;
- each action's `command` property is its row's command;
- the `gui.*` catalog has every command in this file and no others.

Conventions:
- Every command returns `frame` and `masterCycle`, as all commands do (002).
- **Game** means the action needs a loaded game. Without one, it is disabled
  (FR-021) and the command fails with `NoCartridge`.
- Shortcuts are the defaults, and all of them can be rebound (FR-022).
- `Ctrl` is `Cmd` on macOS (Qt's standard mapping). On macOS the items
  marked † move to the application menu.
- Every top-level menu keeps at least one item of its own on every platform.
  macOS hides an empty menu, so a menu whose only items move to the
  application menu would vanish.
- Until its phase lands, an item is present but disabled, with the tooltip
  "Coming in a later update" (tasks G034).
- A command that opens a window returns once the window is shown. It never
  waits for the window to close (R11).
- `slot` is 1-10.

## File

| Item | Command | Arguments | Shortcut | Game |
|---|---|---|---|---|
| Open ROM… | `gui.open_rom` | `path` (optional; without it the file dialog opens) | Ctrl+O | |
| Open Recent ▸ *entry* | `gui.open_recent` | `index` | | |
| Open Recent ▸ Clear List | `gui.clear_recent` | | | |
| Game Library | `gui.show_library` | | Ctrl+Shift+O | |
| Close Game | `gui.close_game` | | Ctrl+W | ✓ |
| Save State ▸ Slot N | `gui.save_state` | `slot`, `name` (optional) | Shift+F1 … Shift+F10 | ✓ |
| Load State ▸ Slot N | `gui.load_state` | `slot` | F1 … F10 | ✓ |
| Quick Save | `gui.quick_save` | `name` (optional) | Ctrl+S | ✓ |
| Quick Load | `gui.quick_load` | | Ctrl+L | ✓ |
| Select Slot ▸ N | `gui.select_slot` | `slot` | Ctrl+1 … Ctrl+0 | |
| Undo Load State | `gui.undo_load_state` | | Ctrl+Z | ✓ |
| Save State to File… | `gui.save_state_file` | `path` (optional) | | ✓ |
| Load State from File… | `gui.load_state_file` | `path` (optional) | | ✓ |
| Import Battery Save… | `gui.import_battery_save` | `path` (optional) | | ✓ |
| Export Battery Save… | `gui.export_battery_save` | `path` (optional) | | ✓ |
| Take Screenshot | `gui.take_screenshot` | | F12 | ✓ |
| Open Library Folder | `gui.open_library_folder` | | | |
| Exit † | `gui.exit` | | Ctrl+Q | |

Import Battery Save replaces the whole battery RAM (a short file is padded
with zeros) and resets the game, which reads its save as it starts; a paused
game stays paused. Both battery items need a cartridge with battery RAM
(`NoBattery`). Take Screenshot writes the core's 256x240 frame, unfiltered,
to `screenshots/<game>/<YYYYMMDD-HHMMSS>.png` and returns its `path`.

`gui.list_slots` (no menu item; it backs the slot submenus' labels) returns
each slot's time and thumbnail file. It is listed under "Queries" below.

Saving from the menus or keys (Save State ▸ Slot N, Quick Save) pauses the
game and asks for the state's name, filled in with the time it was saved.
When the slot already holds a state, the dialog says so and its button is
"Replace". `gui.save_state` and `gui.quick_save` called by automation take an
optional `name` and never ask. The slot menus show each state's name, with
the time it was saved as the tooltip.

Loading a slot or a state file keeps the state it replaced, for one level of
Undo Load State. A load leaves the game paused, as the core does, with
"PAUSED" over the picture; Pause / Resume plays on. A rejected state changes nothing, Undo included. An empty
slot, or nothing to undo, fails with `NotFound`, which the GUI shows as a
status message rather than a dialog. Saving and loading confirm with a
status message.

## Emulation

| Item | Command | Arguments | Shortcut | Game |
|---|---|---|---|---|
| Pause / Resume | `gui.toggle_pause` | | P, Pause | ✓ |
| Reset | `gui.reset` | | Ctrl+R | ✓ |
| Power Cycle | `gui.power_cycle` | | Ctrl+Shift+R | ✓ |
| Frame Advance | `gui.frame_advance` | | \\ | ✓ |
| Fast Forward (hold) | `gui.set_fast_forward` | `on` | Tab (held) | ✓ |
| Fast Forward (toggle) | `gui.toggle_fast_forward` | | Shift+Tab | ✓ |
| Rewind (hold) | `gui.set_rewind` | `on` | Backspace (held) | ✓ |
| Speed ▸ 25 % … 400 %, Unlimited | `gui.set_speed` | `percent` (25, 50, 75, 100, 150, 200, 300, 400) or `unlimited` | | |
| Speed ▸ Slower / Faster | `gui.set_speed` | `step`: `slower` or `faster` (the next listed speed) | − / = | |
| Region ▸ Automatic / NTSC / PAL | `gui.set_region` | `region`: `auto`, `ntsc`, `pal` (G6) | | |

The hold items act while their key is held: on at the press, off at the
release. Chosen from the menu, where nothing is held, they toggle. Speed
scales the frame clock; sound is sped up or slowed with it up to 2x and
muted above that, at Unlimited and while rewinding. An indicator at the top
right of the picture, in the style of PAUSED at half its size, shows "◀◀ REWIND",
"◀◀ FAST REWIND" (rewind with fast-forward held), "▶▶ FAST FORWARD",
"▶▶ UNLIMITED" or a speed other than 100% ("SPEED 50%"). Frame Advance pauses a running game, then runs one frame. Rewind from
a paused game pauses again on release; with no history yet, it says so.

## View

| Item | Command | Arguments | Shortcut | Game |
|---|---|---|---|---|
| Scale ▸ 1x … 6x | `gui.set_scale` | `scale` 1-6 | Alt+1 … Alt+6 | |
| Fullscreen | `gui.toggle_fullscreen` | | F11, Alt+Enter | |
| Integer Scaling | `gui.toggle_integer_scaling` | | | |
| Aspect Ratio ▸ Square Pixels / 8:7 / 4:3 | `gui.set_aspect` | `aspect`: `square`, `8:7`, `4:3` | | |
| Overscan ▸ None / TV (8 lines) / Custom… | `gui.set_overscan` | `preset`, or `top`/`bottom`/`left`/`right` (0-16) | | |
| Filter ▸ Nearest / Smooth / Scanlines | `gui.set_filter` | `filter` | | |
| Palette ▸ *built-in* / Load .pal… | `gui.set_palette` | `name` or `path` (G6) | | |
| Show FPS | `gui.toggle_fps` | | | |
| Show On-Screen Messages | `gui.toggle_osd` | | | |
| Always on Top | `gui.toggle_always_on_top` | | | |

Each View command changes a `video.*` setting (Settings > Video holds them
all) and returns the new value: `scale`, `fullscreen`, `integerScaling`,
`aspect`, `overscan` (`top`, `bottom`, `left`, `right`), `filter` or
`alwaysOnTop`. The menu items are checked to match. A new scale, aspect or
crop resizes the window so the game area is the scale times the cropped,
aspect-corrected picture (rounded up), unless the window is fullscreen or
maximized. Overscan `none` hides nothing and `tv` hides 8 lines at the top
and bottom; `custom` with no edges opens Settings > Video, and any edges
given change just those. In fullscreen on Windows and Linux the menu bar
hides, and shows again at the top edge or with Alt; the shortcuts keep
working.

## Tools

| Item | Command | Arguments | Shortcut | Game |
|---|---|---|---|---|
| Cheats… | `gui.show_cheats` | | | ✓ |
| Cheat Search… | `gui.show_cheat_search` | | | ✓ |
| Movie ▸ Record from Power-On… | `gui.record_movie` | `from`: `power_on`, `path` (optional) | | ✓ |
| Movie ▸ Record from Current State… | `gui.record_movie` | `from`: `state`, `path` (optional) | | ✓ |
| Movie ▸ Play… | `gui.play_movie` | `path` (optional) | | ✓ |
| Movie ▸ Stop | `gui.stop_movie` | | | movie |
| Movie ▸ Read-Only | `gui.toggle_movie_read_only` | | | movie |

Movies (FR-052, data-model "Movie"):
- **Record** writes the file at once, so a folder that cannot take it is
  found before play starts. From power-on, the console is power-cycled
  first; from the current state, the state is embedded. Each frame's input
  (both ports) is kept, and so are Reset and Power Cycle, as events.
- **Play** is refused for another game or another region's timing
  (`MovieMismatch`), before anything changes. A movie from another core
  version plays with a warning. Playback is read-only: the player's buttons
  are ignored. At the last frame the game pauses, so the final picture
  stays on screen; the movie is then closed.
- **Read-Only**, checked while a movie plays. Unchecking it takes over: the
  rest of the movie is dropped, the rerecord count goes up, and the
  player's input records on from that frame.
- While a movie plays, Reset, Power Cycle, every state load (slots, files,
  Undo Load State) and Rewind are refused (`MoviePlaying`). While one
  records, the state loads and Rewind are refused (`MovieRecording`):
  they would break the movie's timeline. Frame Advance records a frame
  like play does.
- Closing the game, or opening another, stops the movie and writes it.
- Not in the movie: cheats, and the battery save a power-on starts from.
  Either can make a replay differ (#121).
| Record Audio… / Stop Recording | `gui.toggle_audio_recording` | `path` (optional) | | ✓ |

The cheat and movie windows' contents have their own commands:
- `gui.add_cheat`, `gui.remove_cheat`, `gui.set_cheat_enabled`,
  `gui.list_cheats`;
- `gui.cheat_search_reset`, `gui.cheat_search_filter`,
  `gui.cheat_search_results`;
- `gui.add_watch`, `gui.remove_watch`, `gui.list_watches` (the per-game
  watch list, with live values); a value is edited with `core.write_memory`,
  which runs at the next frame boundary;
- `gui.movie_status`.

They are listed under "Window commands".

## Debug

| Item | Command | Arguments | Shortcut | Game |
|---|---|---|---|---|
| Debugger | `gui.show_debugger` | | Ctrl+D | ✓ |
| Memory Viewer | `gui.show_memory_viewer` | | Ctrl+M | ✓ |
| PPU Viewer ▸ Pattern Tables / Nametables / Sprites / Palette | `gui.show_ppu_viewer` | `view` | Ctrl+P | ✓ |
| Trace Logger | `gui.show_trace_logger` | | | ✓ |
| Write Memory Dump | `gui.write_memory_dump` | | | ✓ |

The debugger's own controls:
- `gui.debug_step_instruction`, `gui.debug_step_frame`, `gui.debug_run`;
- `gui.add_breakpoint`, `gui.remove_breakpoint`, `gui.list_breakpoints`;
- `gui.trace_start` (`path`) and `gui.trace_stop`.

Register and memory edits use the existing `core.set_cpu` and
`core.write_memory`.

## Settings

| Item | Command | Arguments | Shortcut | Game |
|---|---|---|---|---|
| Settings… † | `gui.show_settings` | none (the last page shown) | Ctrl+, | |
| Video… | `gui.show_settings` | `page`: `video` | | |
| Audio… | `gui.show_settings` | `page`: `audio` | | |
| Input… | `gui.show_settings` | `page`: `input` (turbo rate, opposite directions, pause on disconnect, the gamepads, and a Key Bindings button) | | |
| Hotkeys… | `gui.show_settings` | `page`: `hotkeys` (the Key Bindings page, at the hotkeys) | | |
| Emulation… | `gui.show_settings` | `page`: `emulation` | | |
| Library… | `gui.show_settings` | `page`: `library` | | |
| Servers… | `gui.show_settings` | `page`: `servers` | | |

Every setting can be read and changed without the dialog:
- `gui.get_settings` (optional `group`) returns every value with its schema.
- `gui.set_setting` (`key`, `value`) validates and applies a value.

Update checks (US10, #105) are the setting `updates.check` (off until the
user says yes). No command installs an update.

Each group's keys are an enum in its schema (data-model.md "Settings").

## Help

| Item | Command | Arguments | Shortcut | Game |
|---|---|---|---|---|
| Keyboard Shortcuts | `gui.show_shortcuts` | optional `section`: `hotkeys` or `controls` | | |
| User Guide | `gui.show_user_guide` | | | |
| Open Log Folder | `gui.open_log_folder` | | | |
| About cuteNES † | `gui.show_about` | | | |

## Items from later phases

Every item above is in the menus from G034 on. An item whose feature lands
in a later phase is disabled, with the tooltip "Coming in a later update",
and its command is in the catalog with these arguments but returns
`NotImplemented` (002 command catalog), with `data.phase` naming the phase.
Settings pages that do not exist yet do the same through
`gui.show_settings`.

## Window commands

| Command | Purpose |
|---|---|
| `gui.close_window` | `window` (any that opened by a `gui.show_*`; `all`) |
| `gui.list_windows` | Open windows and dialogs |
| `gui.library_select` | `path` or `identity`: select a game in the browser |
| `gui.add_rom_folder` / `gui.remove_rom_folder` | `path`: scan another folder of ROMs in place, or stop scanning it (FR-002). Overlapping folders and the library's own `roms/` are refused. Returns `romFolders` |
| `gui.add_cheat` / `gui.remove_cheat` / `gui.set_cheat_enabled` / `gui.list_cheats` | Cheat list (data-model "Cheat") |
| `gui.cheat_search_reset` / `gui.cheat_search_filter` / `gui.cheat_search_results` | `filter`: `equal`/`not_equal`/`greater`/`less`/`changed`/`unchanged`, optional `value`; `size` 1 or 2 bytes (reset). Works while the game runs; each snapshot is taken at a frame boundary |
| `gui.add_watch` / `gui.remove_watch` / `gui.list_watches` | The per-game watch list (`address`, `size`, `label`) with current values (FR-053). A value is edited with `core.write_memory`, applied at the next frame boundary |
| `gui.movie_status` | The movie: `mode` (`none`, `recording`, `playing`), `readOnly`, `movieFrame` (frames recorded or played so far; the result's own `frame` is the core's), `frames`, `rerecords`, `path` |
| `gui.debug_*`, `gui.*_breakpoint`, `gui.trace_*` | Debugger, above |

## Queries

| Command | Returns |
|---|---|
| `gui.state` | Window state: library or game, fullscreen, scale, speed, fast-forward, rewind, movie status, selected slot, open windows |
| `gui.list_slots` | For each slot 1-10: `empty`, or `name`, `savedAt` (ISO 8601, UTC), `size`, and the `state` and `thumbnail` file paths; and the game's `selectedSlot` |
| `gui.list_recent` | Recent games with path, identity and whether the file exists |
| `gui.library_list` | The browser's entries (data-model "LibraryEntry"). In GUI mode `session.library_list` returns the same (FR-074) |

## Status bar and indicator (FR-073)

- A paused game shows "PAUSED" over the picture. Short confirmations
  ("State saved to slot 3") float over the bottom-left corner of the window
  and never take room from the game.
- Show FPS puts the frames the game ran in the last second ("60 FPS") at
  the top left of the picture, in the speed indicator's style. With Show
  On-Screen Messages off, the confirmations are not shown.
- There is no status bar (it took room from the game). The **automation
  indicator**, "AUTOMATION", sits at the bottom right of the window in the
  same style. A movie shows under the FPS (or in its place): "● REC 1234"
  while recording, "▶ 120 / 900" while playing.
- The indicator appears while an API or MCP client is there: an event or
  `subscriptions/listen` stream open (an attached `cutenes-server --attach`
  keeps one), a call running, or a call in the last 5 seconds. It reads
  "AUTOMATION · PAUSED" when `pausedByAutomation` is true, and its tooltip
  lists the listeners and says "Paused by automation". Pause / Resume
  plays on.
- Clicking it opens Settings > Servers.

## Settings > Servers (FR-071, FR-072)

- "API" and "MCP server", each with a port (0 picks a free one), the address
  while running, and Copy Token. MCP on turns the API on; API off turns MCP
  off. The keys are `servers.apiEnabled`, `servers.apiPort`,
  `servers.mcpEnabled` and `servers.mcpPort` (`gui.set_setting`).
- Each start makes a new token, written to `api.token` and `mcp.token` in
  the user's state folder (002 R11), where `cutenes-server --attach` finds
  the MCP one. The files go when a listener stops or the window closes.
- A change through the API itself (a client turning the API off) gets its
  reply first; the listeners change right after, and calls still in flight
  end with `ShuttingDown`. The game plays on.
- Servers that were on start with the next launch. One that cannot start (a
  port in use) is turned off, and the user is told.

## Key Bindings page (FR-022, #108)

Every shortcut in this file is a default. One page in the main window lists
and remaps them all, with the game controls. It opens from the library
browser's **Key Bindings** button, from Help > Keyboard Shortcuts
(`gui.show_shortcuts`, at the hotkeys, or with `section` `controls` at the
game controls), from Settings > Hotkeys… (`gui.show_settings` with `page`
`hotkeys`), and from the Key Bindings button of Settings > Input. **Back**, or Esc when no key is being captured, returns to
the screen it came from.

**Entries.** One per menu action with a fixed command and arguments,
including the ones with no default, and one per game button of players 1
and 2, Turbo A and Turbo B included. Each has up to two bindings: two
keys, a key and a gamepad control, or two gamepad controls. An entry's
`id`:
- a menu action: its command, then each argument's value, joined by `.`,
  leaving out the `on` of the hold actions: `gui.quick_save`,
  `gui.save_state.3`, `gui.set_speed.faster`, `gui.show_settings.video`;
- a game button: `p1.a`, `p1.b`, `p1.select`, `p1.start`, `p1.up`,
  `p1.down`, `p1.left`, `p1.right`, `p1.turbo_a`, `p1.turbo_b`, and the
  same with `p2`.

**Bindings** are Qt's portable key text: `Ctrl+Shift+R`, `F1`, `Backspace`
(`Ctrl` is `Cmd` on macOS). A game button takes one key without modifiers;
the Shift, Ctrl, Alt and Meta keys alone are `Shift`, `Ctrl`, `Alt` and
`Meta`. Return also stands for keypad Enter.

A **gamepad binding** is `Pad:` and SDL's standard control names, joined by
`+` in this order: `South`, `East`, `West`, `North`, `Back`, `Guide`,
`Start`, `LeftStick`, `RightStick`, `LeftShoulder`, `RightShoulder`,
`DPadUp`, `DPadDown`, `DPadLeft`, `DPadRight`, `LeftTrigger`,
`RightTrigger`, and the stick directions `LeftStickUp`… `RightStickRight`
(past a dead zone). A game button takes one control, of its player's pad
(so player 1 and player 2 may both have `Pad:South`). A hotkey takes one or
two, from either pad: `Pad:Back+LeftShoulder` acts when the second is
pressed while the first is held, and those controls leave the game until
let go.

**On the page** the user can:
- **rebind**: click a binding (or select it and press Enter) and press a key
  or key combination, or a gamepad control (a game button takes the first
  pressed; a hotkey takes the controls held when the first is let go); Esc
  cancels the capture;
- **add a second binding** in the second column;
- **clear** a binding, or **reset** one entry or all of them to the
  defaults.

**Conflicts** are shown before anything is saved: with another hotkey, with
a game button, or with a platform-reserved shortcut (Cmd+Q, Cmd+H and
Cmd+Tab on macOS; Alt+F4 and Alt+Tab elsewhere; Exit keeps its own Ctrl+Q).
Saving a conflicting binding needs confirmation, and the other entry loses
that binding, so a binding never serves two entries.

**Persistence and display.** Bindings that differ from the defaults are
stored in the `hotkeys` and `input` settings groups, keyed by `id` (an empty
list is a cleared entry). Changes apply at once. The menus always show the
current binding, never the default (US3-3).

**Automation** (FR-070):

| Command | Purpose |
|---|---|
| `gui.list_bindings` | Every entry: `id`, `label` (the menu path, or "Player 1 A"), `kind` (`hotkey` or `control`), and `bindings` and `defaults`, each a pair (`""` for none) |
| `gui.set_binding` | `id`, `binding`, optional `index` (0 or 1, default 0) and `force`. A conflict fails with `InvalidArguments` (`data.path` `/binding`, `data.conflicts` the other entries' `id` and `label`, or `system`) unless `force` is true, which takes the binding from them. Returns the entry |
| `gui.clear_binding` | `id`, optional `index`; without it, both bindings. Returns the entry |
| `gui.reset_bindings` | Optional `id`; without it, every entry. Returns `reset`, the entries that changed |

`gui.get_settings` returns the stored bindings as `hotkeys`, and as
`input.bindings` beside the input options.

## Sound, focus and resuming (FR-036, FR-037, FR-042, FR-044, US7)

- **Settings > Audio**: `audio.device` (a name from `gui.list_audio_devices`,
  or `default`), `audio.volume` (0-100), `audio.muted`, `audio.latencyMs`
  (32-200) and `audio.fastForwardAudio` (`stretch`: sped up to 2x and muted
  above; `mute`). A saved device that is missing at start, or one unplugged
  during play, gives way to the default device with an on-screen message;
  with no device left the game plays on silently.
- **Settings > Emulation**: `emulation.pauseWhenInactive` (on): the game
  pauses while another application has the focus, and resumes when the
  window gets it back, unless the player paused it; cuteNES's own dialogs
  keep it going. `emulation.fastForwardSpeed` (150-400, 300).
  `emulation.resumeOnOpen` (off): closing a game keeps an automatic state
  (`auto.state` in the game's states folder). Opening the game again
  answers `autoState` (its path) and asks "Resume where you left off?",
  with the game paused until the answer; `gui.open_rom` with `resume` true
  loads it (`resumed`), and `resume` false starts afresh without asking.
- **Record Audio** (`gui.toggle_audio_recording`): starts a WAV (16-bit PCM,
  mono, 48 kHz) at `path`, or in the game's folder under the library's
  `recordings`, and returns `recording` and `path`; the next call stops it,
  returning `seconds` too. Closing the game stops it. The menu item reads
  Stop Recording meanwhile.

| Command | Purpose |
|---|---|
| `gui.list_audio_devices` | `devices`: the playback devices' names |

## Gamepads and Settings > Input (FR-032, US5)

The first gamepad plugged in drives player 1 and the second player 2. A
pad's GUID keeps its player while it is away, and across runs, so a pad
plugged back in gets its player back. Unplugging a pad during play shows
"Player N's controller was disconnected" and, with
`input.pauseOnGamepadDisconnect` (on by default), pauses. Outside the game
(the library, the Key Bindings page, dialogs) the D-pad and left stick move
like the arrow keys, South confirms like Enter and East goes back like Esc.

Settings > Input holds `input.turboRate` (2-30 presses a second, 15),
`input.allowOppositeDirections` (off: the last pressed of Left/Right and of
Up/Down wins), `input.pauseOnGamepadDisconnect`, the pads plugged in, and a
Key Bindings button. Turbo toggles on the frame counter while held.

| Command | Purpose |
|---|---|
| `gui.list_gamepads` | `gamepads`: each pad plugged in, with `port` (1 or 2), `name` and `guid` |
| `gui.set_setting` | the `input.*` keys above; a bad value fails with `data.path` `/value` |

## Game input defaults (FR-032)

| Button | Player 1 keyboard | Gamepad |
|---|---|---|
| D-pad | Arrow keys | D-pad (left stick also, with a dead zone) |
| A / B | X / Z | South / West (Xbox A / X) |
| Turbo A / Turbo B | S / A | North / East (Xbox Y / B) |
| Select / Start | Right Shift / Enter | Back / Start |

Player 2 has no keyboard defaults. Its gamepad defaults are the same as
player 1's.

## Errors

`gui.*` commands use the 002 error names:
- `NoCartridge`, `InvalidArguments`, `NotFound`, `LoadError`,
  `LoadStateError`;
- `Busy`, new: another modal window is open, for example a file dialog.

A command never fails silently. On failure, the GUI shows the same message
as an on-screen message or a dialog (FR-023).
