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

## Tools

| Item | Command | Arguments | Shortcut | Game |
|---|---|---|---|---|
| Cheats… | `gui.show_cheats` | | | ✓ |
| Cheat Search… | `gui.show_cheat_search` | | | ✓ |
| Movie ▸ Record… | `gui.record_movie` | `path` (optional), `from`: `power_on` or `state` | | ✓ |
| Movie ▸ Play… | `gui.play_movie` | `path` (optional) | | ✓ |
| Movie ▸ Stop | `gui.stop_movie` | | | ✓ |
| Movie ▸ Read-Only | `gui.toggle_movie_read_only` | | | ✓ |
| Record Audio… / Stop Recording | `gui.toggle_audio_recording` | `path` (optional) | | ✓ |

The cheat and movie windows' contents have their own commands:
- `gui.add_cheat`, `gui.remove_cheat`, `gui.set_cheat_enabled`,
  `gui.list_cheats`;
- `gui.cheat_search_reset`, `gui.cheat_search_filter`,
  `gui.cheat_search_results`;
- `gui.add_watch`, `gui.remove_watch`, `gui.list_watches`.

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
| Input… | `gui.show_settings` | `page`: `input` (the Key Bindings page, at the game controls) | | |
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
  same style, and the movie state (G7) will join the overlays.
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
(`gui.show_shortcuts`), and from Settings > Input… and Settings > Hotkeys…
(`gui.show_settings` with `page` `input` or `hotkeys`, which open the page
at that section). **Back**, or Esc when no key is being captured, returns to
the screen it came from.

**Entries.** One per menu action with a fixed command and arguments,
including the ones with no default, and one per game button of players 1
and 2 (Turbo A and Turbo B join with G4). Each has up to two bindings, so
an action can have two keys, or later a key and a gamepad control. An
entry's `id`:
- a menu action: its command, then each argument's value, joined by `.`,
  leaving out the `on` of the hold actions: `gui.quick_save`,
  `gui.save_state.3`, `gui.set_speed.faster`, `gui.show_settings.video`;
- a game button: `p1.a`, `p1.b`, `p1.select`, `p1.start`, `p1.up`,
  `p1.down`, `p1.left`, `p1.right`, and the same with `p2`.

**Bindings** are Qt's portable key text: `Ctrl+Shift+R`, `F1`, `Backspace`
(`Ctrl` is `Cmd` on macOS). A game button takes one key without modifiers;
the Shift, Ctrl, Alt and Meta keys alone are `Shift`, `Ctrl`, `Alt` and
`Meta`.

**On the page** the user can:
- **rebind**: click a binding (or select it and press Enter) and press a key
  or key combination; Esc cancels the capture;
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

`gui.get_settings` with `group` `hotkeys` or `input` returns the stored
bindings.

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
