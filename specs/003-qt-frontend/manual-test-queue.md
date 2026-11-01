# Manual test queue (feature 003)

The maintainer runs this queue once, when every G0xx task is done
(instruction of 2026-10-31). Until then each GUI PR merges when CI is green
and after Claude has checked it through the MCP tools; its manual steps are
added here. The phase checklists M3-M9 in `quickstart.md` still apply; this
file lists what to try for each merged change, with steps.

Launch: `open build/app/cutenes.app` (add `--args <ROM>` for a game).
Report "passed" or what went wrong for each item.

## G3: menus, states, servers

### Settings > Servers and the automation indicator (G035, PR #110)

Checked through MCP: the bridge attaches, `gui.*` and `core.*` work, and
quitting (`gui.exit`) and reopening the app keeps the same bridge working.

1. Settings > Servers: turn on "MCP server". The API turns on too and the
   page shows the token file path. Copy Token puts the token on the
   clipboard.
2. In Claude Code, with `cutenes-server --attach` registered as `cuteNES`:
   ask it to open a ROM and pause. The status-bar indicator shows while it
   is connected and says when automation paused the game.
3. Quit cuteNES while Claude Code stays open, then reopen it. Ask Claude to
   do something again: it works without `/mcp` reconnect (the bridge
   follows the new token).
4. Start Claude Code while cuteNES is closed, then open cuteNES: the cuteNES
   tools appear in Claude Code within a few seconds.
5. Turn "MCP server" off: Claude's next call says the MCP server is off, and
   the game keeps playing.
6. Open a second copy of cuteNES (`open -n build/app/cutenes.app`) with
   servers off, and close it: the first copy's MCP connection keeps working.

### The Key Bindings page (G036, #108)

Checked through MCP: `gui.set_binding` (a second binding, a refused
conflict), `gui.show_shortcuts` opening the page and pausing the game, and
`gui.reset_bindings`. The key presses on the page are covered by
`GuiKeyBindings` tests.

1. In the library, click **Key Bindings** (top right). The page lists the
   game controls for players 1 and 2, then every menu action grouped by
   menu, with the current keys in the platform's symbols (⌘S on macOS).
2. Click Quick Save's binding and press ⌘⇧K. The row shows it at once, and
   File > Quick Save shows ⌘⇧K.
3. Click Quick Save's second binding and press ⌘L. A question says Quick
   Load uses it. Answer No: nothing changes. Try again and answer Yes:
   Quick Load loses ⌘L.
4. Start a capture and press Esc: the capture ends and nothing changes.
   Press Esc again: you are back where you came from.
5. Bind Player 1 A to K, open a game, and check that K presses A (and X no
   longer does). Bind Player 2 Start to a key and check Start on player 2.
6. Type "slot" in the filter: only the slot actions show.
7. Select a row and press Delete (clears it), then Reset (back to its
   default), then Reset All.
8. Quit and reopen cuteNES: your changes are still there.
9. During a game, Help > Keyboard Shortcuts opens the page and pauses the
   game; Back resumes it. Settings > Input… opens it at the game controls,
   Settings > Hotkeys… at the menu actions.
10. With the page open, use only the keyboard (Tab, arrows, Enter, Esc):
    every control is reachable, and VoiceOver reads each binding.
