# Manual test queue (feature 003)

The maintainer runs this queue once, before a beta or production release
(#190). Each GUI PR merges when CI is green and after Claude has checked it
through the MCP tools; what is left for a person goes here.

## What belongs here

Only what has not been, and cannot be, fully tested by the automated tests
or through the MCP:

- what needs eyes or ears: how something looks or sounds, smoothness,
  legibility, the light and dark themes;
- real hardware: a gamepad, an audio device, a display's refresh rate, a
  Retina screen, a second monitor;
- the operating system itself: Finder, Gatekeeper, the menu bar, the
  clipboard, window focus and stacking, VoiceOver, the browser, native file
  dialogs;
- a real MCP client (Claude Code) starting, stopping and reconnecting;
- a person's judgement: reading the guide as a new player would.

Each section says what is already covered, so the steps stay short. When a
new PR adds a hands-on step, it goes here; a step the MCP could run goes
under "For Claude through the MCP" at the end instead, and Claude runs it.
The phase checklists (M0 to M9) that used to be in `quickstart.md` are
folded in here.

**macOS only.** The maintainer tests on a Mac. Windows and Linux are
supported on a best-effort basis: CI builds, tests and packages them on
every pull request and release, and problems found there come from users'
bug reports (see the README). No step here asks for Windows or Linux.

Launch: `open build/app/cutenes.app` (add `--args <ROM>` for a game), or
the packaged app for the release walkthrough. Report "passed" or what went
wrong for each item.

## First play and the library (M0, M1, M2)

Covered: the window, opening ROMs, pause, reset and power cycle, keys
reaching the controller, the library's creation, scanning, filtering,
Enter, recent games and battery saves across a restart and a rename
(`GuiSkeleton`, `GuiPlay`, `GuiLibraryWindow`, `GuiBattery`).

1. Play a game you own for a few minutes with the keyboard (arrows, Z, X,
   Enter, Right Shift). The picture is sharp at 3x with 8:7 proportions,
   the sound is clean, and input feels immediate.
2. Copy a ROM into `~/Documents/cuteNES/roms` in Finder: it appears in the
   library without a restart. Drag a `.nes` file onto the window: it opens.

## Servers, automation and Claude Code (M3; G035, #110, #111, #233, G091)

Covered: turning the servers on and off, the token files, the indicator
and its automation pause, Disable Automation cutting a client off and
releasing its buttons, servers off at the next launch, the "start with
cuteNES" confirmation and its refusal through automation, a second
window removing only its own token files (`GuiServers`); the bridge
following a new token after a restart, and answering a client that started
first (checked through the MCP, `McpAttach`); both surfaces changing the
state at once (`GuiCoexistence`).

1. Settings > Servers: turn on "MCP server". The page shows the token file
   path, and Copy Token puts the token on the clipboard (paste it
   somewhere).
2. Quit cuteNES, start Claude Code with `cutenes-server --attach`
   registered as `cuteNES`, then open cuteNES and turn on the MCP server.
   Within a few seconds, without `/mcp` reconnect, Claude Code has the
   cuteNES tools. The AUTOMATION indicator shows while it is connected.
3. Play Super Mario Bros. while Claude saves to slot 3 and writes your
   lives at $075A. The picture never glitches or jumps, and the slot menu
   shows Claude's save with its thumbnail.
4. Ask Claude to play for a minute. While it does, click AUTOMATION:
   Settings > Servers opens with Disable Automation at the top. Click it:
   the indicator goes, a message says automation is disabled, and Claude
   reports that cuteNES is not reachable. The game plays on with no button
   stuck down.
5. Disable Automation reads clearly and is greyed out when everything is
   off, in the light and the dark theme.
6. Turn on "Start the servers when cuteNES starts". The confirmation reads
   clearly, is not cut off on a small window, and Enter or Esc does not
   turn it on. Cancel, then turn it on with Turn On, and tell Claude it is
   on: Claude runs the restart check under "For Claude through the MCP".
   Turn it off afterwards.

## Keyboard, Key Bindings and gamepads (M4; G036, G040-G042, #108, #181)

Covered: every binding, rebinding by key press, conflicts, Esc during a
capture, clearing and resetting, persistence, remapped game buttons, the
page from Help and Settings (`GuiKeyBindings`); turbo, opposite
directions, pad bindings, sticks and triggers, ports kept by GUID
(`GuiInput`); the pad driving the library, the game menu, the shoulders
and the UI Navigation group, with a fake pad (`GuiGamepads`).

1. The Key Bindings page lists the keys in macOS symbols (⌘S). Click Quick
   Save's binding and press ⌘⇧K on the real keyboard: the row shows ⌘⇧K,
   and so does File > Quick Save in the menu bar. Reset All afterwards.
2. Use the page with the keyboard alone (Tab, arrows, Enter, Esc): every
   control is reachable, and VoiceOver reads each binding.
3. Plug in a real gamepad: "Player 1: <name>" shows, and Settings > Input
   lists it. In the library the D-pad and left stick move, South opens a
   game, East goes back, North opens the game's menu, and East closes it
   without a click to give the pad back to the library.
4. In a game: South and West are A and B, Back is Select, Start is Start,
   the D-pad and left stick move. Hold North: A fires repeatedly; East is
   turbo B. Both shoulders show the library and pause; East resumes. macOS
   (Game Center, Launchpad) does not take the shoulders.
5. Plug in a second pad: it is player 2. Unplug it during play: the game
   pauses and says "Player 2's controller was disconnected". Plug it back:
   it is player 2 again. Quit and reopen with the pads plugged in the
   other order: each keeps its player.
6. On the Key Bindings page, capture a pad control (the right shoulder for
   Player 1 B) and a pad combination (Back + left shoulder for Quick Save):
   each shows its name and works in a game, and Select is not left pressed.
   Reset All afterwards.

## Display (M5; G050-G051, #97)

Covered: window sizes for every scale, aspect ratio and overscan, each
filter's pixels on the raster renderer, fullscreen and always-on-top
toggling, persistence, the pacing choice and its note (`GuiVideo`); ten
simulated minutes of pacing on 60, 120 and 144 Hz displays (`GuiPacing`);
every setting through the MCP. The Metal renderer needs your eyes.

1. View > Scale 1x to 6x, each aspect ratio, and each filter (Nearest,
   Smooth, Scanlines at 3x or more) in a moving game: the picture fills the
   window with no border, and has no flicker or tearing.
2. On the Retina screen at 2x, Nearest with integer scaling: the pixels are
   sharp and even, with no uneven rows.
3. F11: the picture fills the screen, and the menu bar appears at the top
   edge when you point there. F11 again returns to the window at its scale.
   Unchecking Integer Scaling in fullscreen fills the height.
4. View > Always on Top: the window stays above another app's window.
5. Pacing soak (SC-004): set the display to 60 Hz (System Settings >
   Displays), and play Super Mario Bros. from World 1-1 to 1-4 with
   headphones on, on Automatic pacing. Note any crackle, gap, hitch or
   double step with its time. Settings > Video says "in step with the 60.00
   Hz display". Back on ProMotion (120 Hz), scrolling is as smooth.
6. With an external monitor at another rate (75 or 144 Hz), drag the window
   onto it: the pacing note changes and play stays smooth. Drag it back.

## Sound and focus (M5; G052-G053, #97)

Covered: the audio settings applying and persisting, fast-forward sound,
the WAV's format and length, pausing on focus loss in the test platform,
resuming where you left off (`GuiAudio`); the device list and a recording
through the MCP.

1. Settings > Audio during a game: the volume slider and Mute act at once.
   Latency 32 ms and 200 ms both play without crackles.
2. Pick another output (headphones or AirPods): the sound moves. Disconnect
   it during play: a message says so, the sound goes to the speakers, and
   the game never stops.
3. Fast-forward (Tab) at 200 %: the sound speeds up; with "Mute" chosen
   for fast-forward it goes silent while held.
4. Click another application during a game: it pauses, and the pause menu
   shows only Resume. Click back: it plays on. Pause it yourself with P,
   click away and back: it stays paused. Opening Settings from cuteNES
   does not pause.

## Region and palettes (M6; G6, #98)

Covered: PAL timing at the power cycle, the RGB palette's table, a `.pal`
file colouring the picture and persisting (`GuiRegionPalette`); PAL at
50.007 fps through the MCP.

1. Emulation > Region > PAL, then Power Cycle: the music is lower in pitch.
   Automatic and another power cycle bring it back.
2. View > Palette > Load .pal…: the macOS file dialog opens, and the chosen
   palette colours the picture.

## Cheats and movies (M7; G071-G072, #99, #121)

Covered: the cheat search, edits, freeze, Game Genie, saving per game
(`GuiCheatSearch`, `GuiCheats`); recording, playback, takeover, refusals,
cheats and battery in movies (`GuiMovies`); a cheat search on Super Mario
Bros. and a 1349-frame replay through the MCP.

1. Tools > Cheat Search… on Super Mario Bros.: narrow to your lives
   ($075A) with the window's own controls, double-click the value and type
   9. The game never paused, and the lives counter shows 9.
2. Tools > Movie > Record from Power-On…: the save dialog opens in the
   library's `movies/<game>/` folder with a dated name. "● REC" and "▶ n /
   N" read clearly over the picture.

## Debugging tools (M8; G081-G084, #100)

Covered: breakpoints, stepping against nestest's log, register edits, Step
Frame (`GuiDebugger`); viewing without side effects and editing every
memory space (`GuiMemoryViewer`); the PPU viewer's pictures pixel by pixel
(`GuiPpuViewer`); the trace against nestest's log and the dump
(`GuiTraceLogger`); all of them on Super Mario Bros. through the MCP.

1. Open the Debugger, Memory Viewer, PPU Viewer (each tab) and Trace Logger
   during a game: each is laid out legibly, updates while the game runs,
   and the game keeps full speed and never stutters while you scroll.
2. In the Debugger, set a breakpoint and Run, then close the window: at the
   next stop it comes back by itself, in front.
3. While the game runs, start typing a value into a register field: what
   you type is not overwritten until you press Enter.

## The pause menu (#235)

Covered: the full menu from Pause and only Resume otherwise, Save, Battery
Save, Quit's confirmation, keyboard and pad focus and Esc or East
(`GuiPauseMenu`, `GuiGamepads.ThePadUsesThePauseMenu`).

1. Open a game with a save battery and press P. The menu is legible over a
   bright and a dark picture, PAUSED is in the middle of the picture, and
   Resume has a visible focus ring.
2. Work the menu with the mouse, with the keyboard alone, and with a
   gamepad alone.
3. In fullscreen (F11) the menu is in the middle of the screen and works
   the same way.

## Hiding games (#181)

Covered: `.hiddenroms`, hiding and unhiding, Recent, the game menu's Hide
and Settings > Library (`GuiHiddenGames`).

1. Point at a card: a ⋯ button shows in its top-right corner and stays
   while the pointer moves onto it. Click it: the menu has Hide.
2. Right-click a card, and a row in the list view: the same menu. In the
   list, the ⋯ button sits at the end of the row.
3. With a game selected, Shift+F10 opens the menu on that game.

## The user guide, the log and the README (G094, G094a)

Covered: the guide's tables against the contract, the guide window, the
log's rotation, format, size limit and missing token (`GuiUserGuide`,
`GuiAppLog`); the log folder and its contents through the MCP.

1. Help > User Guide: headings, lists and tables are laid out (not raw `|`
   and `#`), and the library tree is in a fixed-width font. The Model
   Context Protocol link opens your browser.
2. Read the guide as a new player would. Note anything wrong, unclear or
   missing.
3. The README's "Getting started" and "Learn more" read well on GitHub, and
   its link to the guide works.
4. Help > Open Log Folder opens Finder on the folder with `cutenes.log`.

## Updates (G097-G099, #105)

Covered: the feed's choices, the question and the prompt against a fake
feed, Skip and Later, no check with a ROM on the command line
(`UpdateFeed`, `GuiUpdates`); the macOS bundle swap and the whole Install
path on a temporary package (`UpdateInstall`, `GuiUpdates`).

1. The first-launch question ("Check for updates when cuteNES starts?")
   and the update prompt read well. To see the prompt, run a local build
   with `CUTENES_APP_VERSION` lowered below the latest release.
2. Install, once a release above the installed one is offered (the updater
   skips beta and RC tags, so how this runs is open in #232): install the
   previous release's zip, start it with `--fullscreen`, and say Yes to
   update checks. Restart with no game and click Install: a progress bar,
   then the new version starts full screen, and Help > About shows it. At
   the next start no `.cutenes-old.app` is beside the bundle.
3. Turn Wi-Fi off during the download: an error says what failed, and the
   installed version still starts.
4. With the bundle in a folder you cannot write: the prompt has no Install
   and says why.

## The macOS package (M9; G093)

Covered: the CI `package` job builds every package, starts it and runs
`cutenes-server --help`; the macOS zip was checked against SHA256SUMS and
ran from its own frameworks.

1. Download the macOS zip of the latest packaged run and check it against
   SHA256SUMS. Unzip and open cutenes.app: Gatekeeper asks once
   (right-click > Open). The Dock and Finder show the cuteNES icon, and
   Help > About lists the licenses.
2. Double-click a `.nes` file in Finder: the packaged app opens it.
3. Play a session from launch to quit with the packaged app: the library,
   states, a gamepad, sound, and Claude Code attached through the bundle's
   `cutenes-server --attach`. Note anything missing (a plugin, a codec, a
   font).

## For Claude through the MCP

Steps the MCP can run that have not been run yet. Claude runs them when the
MCP server is on, records the result in the PR or #190, and deletes the
step. A step that turns out to need a person moves up into its section.

- **Restart with "start with cuteNES" on (#233)**, after step 6 of
  "Servers": `gui.exit`, relaunch with `open build/app/cutenes.app`, and
  check the cuteNES tools come back with no `/mcp` reconnect.
- **A hidden game keeps its speed when the display sleeps (#124)**: with
  Pause When Inactive off and a game running, `pmset displaysleepnow`, wait
  a minute, and check `core.run_state`'s frame grew by about 60 a second.
  Restore the setting afterwards.
- **Old states still load (#92)**: a state saved by cuteNES 0.2.0 (alpha 2)
  to a file loads into its own game with `gui.load_state_file`.
- **Game Genie on a real game (G071)**: SXIOPO on Super Mario Bros. keeps
  the lives at $075A after a life is lost; HELLO1 is refused.
