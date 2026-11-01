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
