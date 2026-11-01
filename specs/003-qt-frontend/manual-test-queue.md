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
   game; Back resumes it. Settings > Hotkeys… opens it at the menu actions
   (Settings > Input… is now a settings page, see G4).
10. With the page open, use only the keyboard (Tab, arrows, Enter, Esc):
    every control is reachable, and VoiceOver reads each binding.

### Gamepads, turbo and Settings > Input (G040-G042, #96, #108)

Checked through MCP: `gui.list_gamepads` (no pad attached here),
`gui.get_settings` input defaults, `gui.list_bindings` with the pad
defaults and the turbo entries, and Settings > Input opening. The pad
events themselves are covered by `GuiGamepads` tests with a fake pad; these
steps need a real one.

1. Plug in a gamepad: "Player 1: <name>" shows. In the library, the D-pad
   and left stick move the selection, A (South) opens the game, and B
   (East) goes back.
2. In a game, the pad plays as player 1: A/X (South/West) are A/B, Back is
   Select, Start is Start, the D-pad and left stick move.
3. Hold Y (North): A fires repeatedly (turbo A); X/B (East) is turbo B.
   Settings > Input: set the turbo rate to 5 and back to 30; the rate
   follows. Keyboard S and A are turbo A and B too.
4. Hold Left, then press Right: only Right counts (watch a game that
   shows it). Turn on "Allow opposite directions": both count.
5. Plug in a second pad: it drives player 2. Unplug it during play: the
   game pauses and says "Player 2's controller was disconnected". Plug it
   back in: it is player 2 again. Turn off "Pause when a gamepad is
   unplugged" and unplug: play goes on.
6. Quit and reopen with both pads plugged in the other order: each pad
   keeps its player.
7. On the Key Bindings page, click Player 1 B's second binding and press
   the right shoulder: it shows "Pad Right Shoulder", and the shoulder now
   presses B.
8. Click Quick Save's second binding, hold Back and press the left
   shoulder, then let go: it shows "Pad Back + Left Shoulder". In a game
   that combination opens the Quick Save name dialog, and Select is not
   left pressed.
9. Settings > Input lists the pads plugged in, and Key Bindings… opens the
   page at the game controls. The Settings list has no Hotkeys entry; the
   Settings > Hotkeys… menu item still opens the page.

### Video settings (G050-G051, #97)

Checked through MCP: `gui.set_scale`, `gui.set_filter`, `gui.set_overscan`,
`gui.toggle_fullscreen` (in and out) and `gui.get_settings` video, during a
game. Window sizes and each filter's pixels are covered by `GuiVideo` tests
on the raster renderer; the GPU renderer (Metal here) needs your eyes.

1. View > Scale > 1x … 6x: the window resizes each time, the picture fills
   it with no border, and the checked item follows.
2. View > Aspect Ratio: Square Pixels is narrower than 8:7; 4:3 is wider.
   Overscan > None shows 8 more lines at the top and bottom than TV.
3. View > Filter > Smooth blurs the pixel edges; Scanlines darkens the
   lower part of every line (look closely at 3x or more); Nearest is sharp.
   Try each in a moving game: no flicker or tearing.
4. View > Overscan > Custom… opens Settings > Video. Change each edge: the
   picture crops at once and the window resizes.
5. F11 (or View > Fullscreen): the picture fills the screen. On macOS the
   menu bar appears at the top edge. F11 again returns to the window at its
   scale. Uncheck Integer Scaling in fullscreen: the picture grows to fill
   the height.
6. View > Always on Top: the window stays above another app's window.
7. Quit and reopen: every video setting is as you left it, fullscreen
   included.
8. On a Retina screen at 2x, the pixels are sharp and even (no uneven
   rows), with Nearest and integer scaling on.

### Sound, focus and resuming (G052-G053, #97)

Checked through MCP: `gui.list_audio_devices` (it listed the MacBook
speakers and Teams Audio), and Record Audio over 120 frames gave a WAV of
1.9967 s, which `afinfo` reads as 48 kHz mono 16-bit. Settings, the WAV
header and length, focus pausing and resuming are covered by `GuiAudio`
tests.

1. Settings > Audio: move the volume slider during a game: the sound
   follows at once. Mute silences it; unmute brings it back.
2. Pick another output device (headphones, AirPods): the sound moves to
   it. Unplug or disconnect it during play: a message says so, the sound
   goes to the speakers, and the game never stops.
3. Set the latency to 32 ms, then 200 ms: both play without crackles (200
   lags a little behind the picture).
4. Fast-forward (Tab) at 200 % (Settings > Emulation): the sound speeds up.
   Choose "Mute" for fast-forward sound: it goes silent while held.
5. Tools > Record Audio, play for a while, Tools > Stop Recording: a WAV
   in the library's recordings folder plays back what you heard.
6. Click another application during a game: the game pauses. Click back:
   it plays on. Pause it yourself, click away and back: it stays paused.
   Open Settings from cuteNES during a game: it keeps playing.
7. Turn on "Resume games where I left off", play a little, close the game,
   and open it again: you are asked, and Yes goes on from where you were;
   No starts afresh.

### Frame pacing (G051, #97)

Checked through MCP: on the MacBook's 120 Hz display, `video.pacingNow`
reported sync to display with a frame every second refresh (60 fps);
`video.pacing` `audio` went back to 60.0988 fps. The choice rules are
covered by `GuiFrameClock` and `GuiVideo` tests.

1. Settings > Video > Frame pacing shows "Automatic" and, under it, what it
   is doing ("a frame every 2 refreshes of the 120.00 Hz display" on a ProMotion
   MacBook, or "in step with the 60.00 Hz display" on a 60 Hz monitor).
2. Play a side-scroller (Mega Man 2's intro, or a level) for a minute on
   Automatic: the scrolling is smooth, with no regular hitch, and the
   sound has no crackle.
3. Switch to "Sync to audio" and compare: on a 60 Hz display you may see a
   small hitch every several seconds; Automatic should look as good or
   better.
4. With an external monitor at a different rate (75 or 144 Hz), drag the
   window onto it: the note changes to the console's own rate and play
   stays smooth. Drag it back: it follows the display again.

### Region, palettes and Game Genie (G6, #98)

Checked through MCP: with Emulation > Region on PAL, Mega Man 2 (an NTSC
cartridge) ran as PAL at 50.007 fps, and the RGB PPU palette coloured the
picture; switching back to Automatic during play reported that it applies
at the next power cycle. Settings were restored afterwards. Game Genie has
no menu yet (G7 adds the cheat list); it is tested through
`core.set_game_genie`.

1. Emulation > Region > PAL during a game: a message says PAL timing starts
   at the next power cycle. Emulation > Power Cycle: the game runs about
   17% slower, and its music is lower in pitch. Automatic and another power
   cycle bring it back.
2. View > Palette > RGB PPU (2C03): the colours change at once (brighter,
   flatter); Default brings the usual colours back. The check mark follows.
3. View > Palette > Load .pal…, choose any 192-byte .pal file you have: the
   picture takes its colours, and Load .pal… is checked. Quit and reopen:
   the same palette is in use.
4. Take a screenshot with a non-default palette: it shows the colours you
   see.

### Cheats and the cheat search (G071, #99)

Checked through MCP: on Mega Man 2 the search covered 10240 bytes (RAM and
PRG-RAM), one "changed" filter left 13, and the game kept running with the
search window open. Narrowing by value, 2-byte values, edits, freeze, set,
Game Genie and saving per game are covered by the `GuiCheatSearch` and
`GuiCheats` tests on a counter ROM.

1. Super Mario Bros.: Tools > Cheat Search…, New Search (1 byte). Filter
   "Equal to" 3 (your lives). Lose a life, filter "Equal to" 2. Repeat until
   one address is left (it should be $075A). The game never paused.
2. Select it, type a label ("Lives"), press Freeze: Tools > Cheats… lists
   it, switched on. Lose a life: the count stays.
3. In the results, double-click the value and type 9: the lives counter
   shows it at once, while the game runs.
4. Press Watch: the watch list shows the address with its live value; type
   a new value there too.
5. Tools > Cheats…, Kind "Game Genie", code SXIOPO (Super Mario Bros.
   infinite lives), Add: it works as the accessory did. A wrong code, like
   HELLO1, is refused with the reason.
6. Untick a cheat: it stops. Close and reopen the game: the cheats and the
   watch list are back, ticked as you left them.
