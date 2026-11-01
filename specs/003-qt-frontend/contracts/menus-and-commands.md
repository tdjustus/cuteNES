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
  `gui.cheat_search_results`.

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
| Input… | `gui.show_settings` | `page`: `input` | | |
| Hotkeys… | `gui.show_settings` | `page`: `hotkeys` | | |
| Emulation… | `gui.show_settings` | `page`: `emulation` | | |
| Library… | `gui.show_settings` | `page`: `library` | | |
| Servers… | `gui.show_settings` | `page`: `servers` | | |

Every setting can be read and changed without the dialog:
- `gui.get_settings` (optional `group`) returns every value with its schema.
- `gui.set_setting` (`key`, `value`) validates and applies a value.

Each group's keys are an enum in its schema (data-model.md "Settings").

## Help

| Item | Command | Arguments | Shortcut | Game |
|---|---|---|---|---|
| Keyboard Shortcuts | `gui.show_shortcuts` | | | |
| User Guide | `gui.show_user_guide` | | | |
| Open Log Folder | `gui.open_log_folder` | | | |
| About cuteNES † | `gui.show_about` | | | |

## Window commands

| Command | Purpose |
|---|---|
| `gui.close_window` | `window` (any that opened by a `gui.show_*`; `all`) |
| `gui.list_windows` | Open windows and dialogs |
| `gui.library_select` | `path` or `identity`: select a game in the browser |
| `gui.add_rom_folder` / `gui.remove_rom_folder` | `path`: scan another folder of ROMs in place, or stop scanning it (FR-002). Overlapping folders and the library's own `roms/` are refused. Returns `romFolders` |
| `gui.add_cheat` / `gui.remove_cheat` / `gui.set_cheat_enabled` / `gui.list_cheats` | Cheat list (data-model "Cheat") |
| `gui.cheat_search_reset` / `gui.cheat_search_filter` / `gui.cheat_search_results` | `filter`: `equal`/`not_equal`/`greater`/`less`/`changed`/`unchanged`, optional `value` |
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
- The status bar shows the game name, the FPS (when on), the speed when it is
  not 100%, the movie state, and an **automation indicator**.
- The indicator appears while any API or MCP client is connected. Its
  tooltip lists the clients and says "Paused by automation" when
  `pausedByAutomation` is true.
- Clicking it opens Settings > Servers.

## Remapping shortcuts (FR-022)

Every shortcut in this file is a default. Settings > Hotkeys lists every
menu action, including the ones with no default, and lets the user:
- **Rebind** an action. Click its binding and press a key combination or a
  gamepad button; gamepad combinations such as Select+R are allowed, so an
  action can be reached without a keyboard.
- **Add a second binding**, so an action can have a keyboard and a gamepad
  binding at once.
- **Clear** a binding, or **reset** one action or all of them to the
  defaults.

**Conflicts** are shown before they are saved: with another hotkey, with a
game button binding, or with a platform-reserved shortcut such as Cmd+Q on
macOS. Saving a conflicting binding needs confirmation, and the other
action loses that binding.

**Persistence and display.** Bindings persist in the `hotkeys` settings
group. The menus always show the current binding, never the default
(US3-3).

**Automation.** `gui.get_settings` with `group` `hotkeys` and
`gui.set_setting` (`hotkeys.<command>`) read and change the bindings
(FR-070), and a reset is `gui.set_setting` with value `default`.

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
