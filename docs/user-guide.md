# cuteNES User Guide

cuteNES plays NES games on Windows, macOS and Linux. This guide covers the
desktop application: finding your games, playing, saving, the settings, and
the tools for cheats, movies and debugging. It is also in the application,
under **Help > User Guide**.

On macOS, read **Ctrl** as **Cmd** throughout. Items marked † are in the
cuteNES application menu on macOS rather than the menu named here.

## Getting started

1. **Start cuteNES.** The first time, it makes a library folder:
   `Documents/cuteNES`. It holds your games, saves, screenshots and
   everything else cuteNES writes.
2. **Add your games.** Put `.nes` files in the library's `roms` folder
   (subfolders are fine), or click **Add Folder…** in the library to use a
   folder you already have. cuteNES only reads those folders; it never
   changes or moves your ROM files.
3. **Pick a game** in the library and press Enter, or double-click it. You
   can also drag a `.nes` file onto the window, use **File > Open ROM…**
   (Ctrl+O), or open one from **File > Open Recent**.
4. **Play** with the keyboard (see "Game controls" below) or a gamepad.

To go back to the library while playing, use **File > Game Library**
(Ctrl+Shift+O). The game pauses while you browse.

Only play games you own. cuteNES does not come with any games.

## Playing

- **Pause / Resume** with P or Pause. "PAUSED" shows over the picture.
  When another application has the focus the game pauses too, and resumes
  when you come back (you can turn this off in Settings > Emulation).
- **Reset** (Ctrl+R) presses the console's reset button. **Power Cycle**
  (Ctrl+Shift+R) turns it off and on.
- **Fast forward**: hold Tab, or press Shift+Tab to keep it on. The speed
  is set in Settings > Emulation.
- **Rewind**: hold Backspace to run the game backwards. Hold Tab as well to
  rewind faster.
- **Speed**: Emulation > Speed sets 25% to 400% or Unlimited; − and = step
  down and up. Sound follows the speed up to twice normal and is muted
  above that.
- **Frame Advance** (\\) runs exactly one frame, pausing first if needed.
  It is useful for studying a moment closely.
- **Region**: Emulation > Region. Automatic follows the game's header; NTSC
  and PAL force a console type.

Closing a game (Ctrl+W) saves its battery save. With **Resume where you
left off** on (Settings > Emulation), cuteNES also keeps the moment you
closed it and offers to return there next time.

## Saving

### Battery saves

Games with a save battery (cartridges that remember your progress) save
automatically to the library's `saves` folder, just as the cartridge would.
**File > Import Battery Save…** and **Export Battery Save…** move a save in
or out, for example from another emulator. Importing restarts the game so it
reads the save.

### Save states

A save state is a snapshot of the whole console, so you can return to that
exact moment. Each game has ten slots.

- **Save** to a slot with Shift+F1 to Shift+F10, and **load** one with F1
  to F10. cuteNES asks for a name, filled in with the time.
- **Quick Save** (Ctrl+S) and **Quick Load** (Ctrl+L) use the selected
  slot. Choose it with Ctrl+1 to Ctrl+0 (slot 10).
- **Undo Load State** (Ctrl+Z) brings back what you had before the last
  load, if you loaded by mistake.
- A loaded state starts paused. Press P to play.
- **Save State to File…** and **Load State from File…** keep a state
  anywhere you like.

The File menu's slot lists show each state's name; hover for the time it
was saved.

## Controls

### Game controls

| Button | Player 1 keyboard | Gamepad |
|---|---|---|
| D-pad | Arrow keys | D-pad (left stick also, with a dead zone) |
| A / B | X / Z | South / West (Xbox A / X) |
| Turbo A / Turbo B | S / A | North / East (Xbox Y / B) |
| Select / Start | Right Shift / Enter | Back / Start |

Player 2 has no keyboard keys until you give it some. The first gamepad you
plug in is player 1, the second player 2, and each pad keeps its player
when you plug it in again. If a pad is unplugged during play, the game
pauses (Settings > Input can turn that off).

Turbo buttons press A or B repeatedly while held; set how fast in
Settings > Input. In the library and dialogs a gamepad moves like the arrow
keys, South confirms and East goes back.

### Changing keys

Every key and gamepad button can be changed on the **Key Bindings** page:
**Help > Keyboard Shortcuts**, the library's **Key Bindings** button, or
**Settings > Input > Key Bindings**.

- Click a binding (or select it and press Enter), then press the new key,
  key combination or gamepad button. Esc cancels.
- Each entry can have two bindings, for example a key and a gamepad button.
- A gamepad hotkey can be a pair of buttons, such as Back + Left Shoulder:
  hold the first and press the second.
- If the binding is already used, cuteNES says where and asks before moving
  it. **Reset** puts an entry, or everything, back to the defaults.

The menus always show your current keys.

## Keyboard shortcuts

These are the defaults; all of them can be changed (see "Changing keys").

### Menu shortcuts

| Menu | Item | Shortcut |
|---|---|---|
| File | Open ROM… | Ctrl+O |
| File | Game Library | Ctrl+Shift+O |
| File | Close Game | Ctrl+W |
| File | Save State ▸ Slot N | Shift+F1 … Shift+F10 |
| File | Load State ▸ Slot N | F1 … F10 |
| File | Quick Save | Ctrl+S |
| File | Quick Load | Ctrl+L |
| File | Select Slot ▸ N | Ctrl+1 … Ctrl+0 |
| File | Undo Load State | Ctrl+Z |
| File | Take Screenshot | F12 |
| File | Exit † | Ctrl+Q |
| Emulation | Pause / Resume | P, Pause |
| Emulation | Reset | Ctrl+R |
| Emulation | Power Cycle | Ctrl+Shift+R |
| Emulation | Frame Advance | \\ |
| Emulation | Fast Forward (hold) | Tab (held) |
| Emulation | Fast Forward (toggle) | Shift+Tab |
| Emulation | Rewind (hold) | Backspace (held) |
| Emulation | Speed ▸ Slower / Faster | − / = |
| View | Scale ▸ 1x … 6x | Alt+1 … Alt+6 |
| View | Fullscreen | F11, Alt+Enter |
| Debug | Debugger | Ctrl+D |
| Debug | Memory Viewer | Ctrl+M |
| Debug | PPU Viewer ▸ Pattern Tables / Nametables / Sprites / Palette | Ctrl+P |
| Settings | Settings… † | Ctrl+, |

## The picture and sound

The **View** menu changes how the game looks; Settings > Video has the same
options in one place.

- **Scale** 1x to 6x (Alt+1 to Alt+6) sizes the window to the picture.
  **Fullscreen** is F11 or Alt+Enter. In fullscreen on Windows and Linux the
  menu bar hides; move the mouse to the top edge or press Alt to see it.
- **Integer Scaling** keeps every NES pixel the same size.
- **Aspect Ratio**: Square Pixels, 8:7, or 4:3 like a television.
- **Overscan**: old televisions hid the picture's edges, and some games put
  junk there. TV hides 8 lines at the top and bottom; Custom… sets each
  edge.
- **Filter**: Nearest (sharp), Smooth, or Scanlines.
- **Palette**: the NES has no single "correct" set of colours. Pick a
  built-in palette or load a `.pal` file.
- **Show FPS** and **Show On-Screen Messages** turn the frame counter and
  the short confirmations ("State saved to slot 3") on and off.
- **Take Screenshot** (F12) saves the game's picture, at the console's own
  256 x 240, to the library's `screenshots` folder.

**Settings > Audio** picks the output device, the volume and the latency.
Lower latency makes sound respond faster but may crackle on a busy computer.
If the device goes away, cuteNES switches to the default one. **Tools >
Record Audio…** saves the game's sound as a WAV file in the library's
`recordings` folder; choose it again to stop.

## Settings

**Settings…** (Ctrl+,) has a page for each area:

| Page | What it holds |
|---|---|
| Video | Scale, fullscreen, aspect ratio, overscan, filter, palette, and how frames are paced to your display |
| Audio | Device, volume, mute, latency, and what fast forward does to the sound |
| Input | Turbo speed, whether Left+Right or Up+Down can be pressed together, pausing when a pad is unplugged, the gamepads plugged in, and Key Bindings |
| Hotkeys | Opens the Key Bindings page at the hotkeys |
| Emulation | Pausing when another application has the focus, fast-forward speed, and resuming where you left off |
| Library | The library folder's location, and your other ROM folders |
| Servers | The automation API and MCP server (see "Connecting an AI agent") |

Changes apply at once. To use another library folder for one run only,
start cuteNES with `--library DIR`.

## Cheats

**Tools > Cheats…** keeps a list of cheats for each game, each switched on
or off with its checkbox. A cheat is one of:

- **Game Genie**: a code from the Game Genie book or the web.
- **Freeze**: holds a memory address at a value every frame (infinite
  lives, for example).
- **Set**: writes a value once.

Cheats are kept per game in the library's `cheats` folder.

**Tools > Cheat Search…** finds the address of something you can see
changing, such as lives or health:

1. Click **New Search** while playing.
2. Play until the value changes (lose a life), then **Filter** for values
   that are less than, greater than, equal to or not equal to before (or a
   number you type), changed or unchanged.
3. Repeat until a few addresses are left, then select one and click
   **Freeze** to keep it at its value as a cheat.

## Movies

A movie records every button press from a starting point, so it replays
exactly the same way. Use **Tools > Movie**:

- **Record from Power-On…** restarts the console and records from the first
  frame. **Record from Current State…** starts from now.
- **Play…** replays a movie made for the same game and region. While it
  plays your buttons are ignored, and the game pauses on the last frame.
- **Read-Only**: uncheck it during playback to take over and record from
  that point.
- **Stop** ends recording or playback. Closing the game stops it too.

While a movie plays or records, loading states and rewinding are refused,
because they would break the movie. Cheats and the battery save a game
starts with are not part of a movie, so either can make a replay differ.

## Debugging tools

The **Debug** menu is for looking inside a game. Each tool opens its own
window, and the game keeps running unless you pause it.

- **Debugger** (Ctrl+D): the CPU's registers and flags, the code around the
  current instruction, breakpoints (the game stops just before the
  instruction at that address), and stepping by instruction or frame.
  Edit a register by typing a new value in hex.
- **Memory Viewer** (Ctrl+M): CPU memory, PPU memory, sprite memory (OAM)
  and palette memory, 16 bytes to a row. Type a byte in hex to change it.
- **PPU Viewer** (Ctrl+P): the pattern tables (the game's tiles), the four
  nametables with the visible screen marked, the 64 sprites, and the
  palette.
- **Trace Logger**: writes every instruction the CPU runs to a file in the
  library's `debug` folder. A running game makes about 55 MB a second, so
  log only as long as you need.
- **Write Memory Dump**: saves all of the console's memory to the `debug`
  folder.

## Connecting an AI agent

cuteNES can be driven by AI agents and scripts through the
[Model Context Protocol](https://modelcontextprotocol.io) (MCP). The agent
sees the same game you do, and you can play while it watches or helps.

1. Open **Settings > Servers** and turn on **MCP server**.
2. Point your MCP client at `cutenes-server --attach`. It finds the running
   cuteNES window by itself. `cutenes-server` is next to `cutenes` (on
   macOS, inside `cutenes.app/Contents/MacOS`). For example, with Claude
   Code:
   `claude mcp add cutenes -- /path/to/cutenes-server --attach`.
3. While an agent is connected, **AUTOMATION** shows at the bottom right of
   the window. Click it to open Settings > Servers.

Turning the server off disconnects the agent; the game keeps playing. The
**API** switch on the same page is a local JSON interface for your own
scripts. Each start makes a new access token (**Copy Token**), and the
servers listen only on this computer.

## The library folder

```text
cuteNES/
├── roms/          your games
├── saves/         battery saves
├── states/        save states, one folder per game
├── screenshots/   F12 pictures
├── movies/        recorded movies
├── recordings/    recorded sound
├── cheats/        cheat lists
└── debug/         trace logs and memory dumps
```

**File > Open Library Folder** shows it in your file manager. To move it,
use Settings > Library.

## Command line

```text
cutenes [--fullscreen] [--library DIR] [--load-state SLOT|FILE]
        [--api [--api-port N]] [--mcp [--mcp-port N]]
        [--help] [--version] [ROM]
```

- `ROM` starts that game straight away, and `--load-state` then loads a slot
  (1-10) or a state file.
- `--library DIR` uses another library folder for this run only.
- `--api` and `--mcp` turn the servers on for this run only.

## Troubleshooting

- **No sound.** Check Settings > Audio: the device, the volume, and that
  Mute is off. Sound is muted while rewinding and above 2x speed.
- **Crackling sound.** Raise the latency in Settings > Audio.
- **The game stops when I click another window.** That is the pause when
  another application has the focus; turn it off in Settings > Emulation.
- **A key does nothing.** Check its binding on the Key Bindings page. A key
  can serve only one entry.
- **A game will not open.** cuteNES says why: a damaged file, or a mapper
  (cartridge hardware) it does not support yet.
- **macOS says the app cannot be checked.** cuteNES is not signed with an
  Apple developer certificate. Right-click the app, choose Open, and confirm
  once.
- **Windows SmartScreen warns about the app.** Choose More info, then Run
  anyway.

### Reporting a problem

**Help > Open Log Folder** shows cuteNES's log: `cutenes.log` for this run
and `cutenes.previous.log` for the one before, so if cuteNES went wrong and
you restarted it, the earlier file has what happened. Attach the right one
to a bug report on
[GitHub](https://github.com/tdjustus/cuteNES/issues). The log holds file
paths (the games you opened) but never your server tokens; look it over
before sharing it.
