# Feature Specification: Qt Desktop Frontend

**Feature Branch**: `003-qt-frontend`

**Created**: 2026-10-30

**Status**: Draft

**Input**: User description: "lets write the spec based on preferred gui
practices for emulators and make sure the menus include access to all of the
features. the gui should also have a library folder that stores their save
states, etc. and the user can place their roms in for quick discovery, but any
location can be used. there should be a menu system to select a rom from quick
discovery if a rom file is not passed to the program via cli." Also issue #32:
the GUI-mode requirements that feature 002 handed over.

## Clarifications

### Session 2026-10-30

- Q: Where is the library folder created by default? → A: `cuteNES` in the
  user's Documents folder. Settings stay in the platform's per-user settings
  location (FR-010, FR-014).
- Q: Region override, palette selection and Game Genie need the core. Are
  they in 1.0? → A: Yes. Each is a feature-001 core amendment, done
  test-first, and then exposed in the menus and as `core.*` commands.
- Q: Which ROM files are accepted? → A: `.nes` only (iNES 1.0 and NES 2.0).
  Archives are out of scope.
- Q: Are video recording, NTSC composite filters, shader packs and portable
  mode out of 1.0? → A: Yes. 1.0 has PNG screenshots, WAV audio recording,
  and nearest, smooth and scanline filters.

### Session 2026-10-31

- Q: Can cuteNES update itself? → A: Yes, in 1.0, and opt-in only. The
  first launch asks whether to turn it on. With it on, each launch (never
  while a game runs) looks at GitHub Releases for a newer build for this
  platform and offers Install, Ask Again Later, Skip This Version or See
  Details. Install replaces the current version, quits, and starts the new
  one with the same arguments (US10, FR-090 to FR-097, #105).

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Pick a game and play it (Priority: P1)

As a player, I start cuteNES. If I passed a ROM on the command line, the game
starts at once. Otherwise I see my game library: every ROM in my library
folder, plus the games I played recently from anywhere else. I pick one with
the mouse, the keyboard or a gamepad, and it plays at full speed with sound,
correct proportions and responsive controls.

**Why this priority**: This is the product. Every other story builds on a game
that is running in a window.

**Independent Test**: With an empty settings profile and a library folder
holding a few public-domain test ROMs:
- Launch with no arguments, choose a ROM from the library and play it.
- Launch again with a ROM path that is outside the library; it starts
  directly.
- Use File > Open ROM to open a ROM from a third folder.

**Acceptance Scenarios**:

1. **Given** no ROM argument, **When** cuteNES starts, **Then** the library
   browser is shown. It lists every ROM under the library's ROM folder, and a
   "Recent" group lists games opened from other folders.
2. **Given** a ROM path argument, **When** cuteNES starts, **Then** that game
   boots immediately and the library browser is skipped.
3. **Given** the library browser, **When** the user types part of a name,
   **Then** the list filters to matching games. Arrow keys or a gamepad
   D-pad move the selection, and Enter or the gamepad's confirm button starts
   the selected game.
4. **Given** a ROM the core cannot run (an unsupported mapper or a damaged
   header), **When** it is listed, **Then** it is marked as unsupported with
   the reason. **When** the user tries to open it, **Then** a message
   explains why and any game already running keeps running.
5. **Given** a running game, **When** the user drops another ROM file on the
   window or picks one under File > Open Recent, **Then** the first game's
   battery save is written and the new game starts.
6. **Given** a running game, **When** File > Close Game is chosen, **Then**
   the battery save is written and the library browser returns.

---

### User Story 2 - Saves live in one library folder (Priority: P1)

As a player, I want one visible folder that holds my ROMs and everything the
emulator creates for them: battery saves, save states, screenshots, movies,
recordings, cheats and debug dumps. I can back it up, sync it or move it. A
ROM opened from anywhere else still keeps its saves in the library.

**Why this priority**: Losing a save is the worst thing an emulator can do.
A predictable, user-visible place for saves is what players expect.

**Independent Test**:
1. Play a battery-backed test ROM until it writes its RAM, then close it.
2. Confirm that the save file appears in the library's save folder.
3. Move the ROM to another folder, rename it, and open it from there.
4. Confirm that the save is found and loaded.

**Acceptance Scenarios**:

1. **Given** a first launch, **When** cuteNES starts, **Then** it creates the
   library folder at the default location with its subfolders (FR-010), and
   the library browser shows how to add ROMs (open the ROM folder, or change
   the library location).
2. **Given** a battery-backed game, **When** the game is closed, the window
   is closed or another game is opened, **Then** its battery RAM is written
   to the library. **When** the game changes its battery RAM during play,
   **Then** the save on disk is updated within 10 seconds, so a crash loses
   at most that much.
3. **Given** a game opened from outside the library, **When** it is saved,
   **Then** its battery save and states go to the library, keyed to the game.
   **When** the same ROM is later opened from another path or under another
   name, **Then** the same saves are used.
4. **Given** the user changes the library location in Settings, **When**
   they confirm, **Then** cuteNES offers to move the existing contents. The
   old folder is never deleted without asking.
5. **Given** a library folder that is missing or not writable at start-up,
   **When** cuteNES starts, **Then** it says so and offers to recreate it or
   to choose another location. It never silently writes saves somewhere
   else.

---

### User Story 3 - Every feature is in the menus (Priority: P1)

As a player, I find every feature of the emulator in the menu bar, grouped
the way other desktop emulators group them, with the keyboard shortcut shown
next to each item. Nothing needs a hidden key combination, a command-line
flag or a config file edit.

**Why this priority**: The user asked for it explicitly, and it is the
cornerstone of discoverability. It is also the inventory that the automation
parity requirement (US6) is checked against.

**Independent Test**: An automated audit walks the menu bar. Every action
listed under "Menu structure" (FR-020) exists, is enabled or disabled
correctly for the current state (no game, running, paused), shows its
shortcut, and works when triggered.

**Acceptance Scenarios**:

1. **Given** the menu bar, **When** it is inspected, **Then** it contains the
   File, Emulation, View, Tools, Debug, Settings and Help menus with the items
   in FR-020. On macOS, About, Settings and Quit move to the application menu
   as the platform requires.
2. **Given** no game is loaded, **When** a menu opens, **Then** the actions
   that need a game (reset, save state, debuggers) are disabled, not hidden.
3. **Given** any action that has a keyboard shortcut, **When** its menu
   opens, **Then** the shortcut is shown next to it. **When** the user
   rebinds it in Settings > Hotkeys, **Then** the menu shows the new binding.
4. **Given** fullscreen, **When** the user presses Alt (or moves the pointer
   to the top edge), **Then** the menu bar becomes reachable without leaving
   fullscreen.

---

### User Story 4 - Save states, rewind and speed (Priority: P2)

As a player, I can save and load states in numbered slots with a quick key,
see a thumbnail and time for each slot before I load it, undo an accidental
load, rewind a few seconds after a mistake, fast-forward through slow parts,
and step one frame at a time.

**Why this priority**: These are the conveniences people choose an emulator
for, and the core already provides the state capture they need.

**Independent Test**: Run a test ROM, save to slot 3, play on, load slot 3,
and check that the screen and frame hash match the moment of saving. Hold
rewind for 2 seconds and check that the frame counter went back about 120
frames.

**Acceptance Scenarios**:

1. **Given** a running game, **When** the user saves to slot N (1-10),
   **Then** the state, a thumbnail and the time are stored in the library,
   and an on-screen message confirms it. **When** slot N is loaded, **Then**
   the game is exactly where it was saved.
2. **Given** the Save State and Load State submenus, **When** they open,
   **Then** each slot shows its time and a thumbnail, or "Empty".
3. **Given** a state was just loaded, **When** the user chooses Undo Load
   State, **Then** the game returns to the moment just before the load.
4. **Given** rewind is held, **When** the game has been running for at least
   the rewind length, **Then** play runs backwards smoothly, and releasing it
   resumes from that point.
5. **Given** fast-forward is held or toggled, **Then** the game runs at the
   chosen fast-forward speed and audio is either sped up or muted (a
   setting). Releasing it returns to normal speed.
6. **Given** a paused game, **When** Frame Advance is pressed, **Then**
   exactly one frame runs.
7. **Given** a state saved by another core version that cannot be loaded,
   **When** it is loaded, **Then** the reason is shown and the game in
   progress is untouched.

---

### User Story 5 - Input that works with keyboards and gamepads (Priority: P2)

As a player, I can play with a keyboard or with one or two gamepads, remap
every button by pressing the key I want, use turbo buttons, and plug a
gamepad in or out while playing.

**Why this priority**: The defaults must work out of the box, and remapping
is expected of every emulator.

**Independent Test**: In Settings > Input, rebind player 1's A to a different
key and confirm, through `core.get_controller`, that the game sees A when
that key is held. Connect a gamepad and confirm its defaults drive player 1.

**Acceptance Scenarios**:

1. **Given** a first launch, **Then** player 1 has keyboard defaults
   (arrows; Z = B, X = A; Right Shift = Select; Enter = Start) and the first
   gamepad is mapped to player 1 with its standard layout. A second gamepad
   is mapped to player 2.
2. **Given** the input settings, **When** the user clicks a button's binding
   and presses a key or a gamepad control, **Then** that becomes the binding.
   A conflict with another binding or a hotkey is shown.
3. **Given** turbo A or turbo B is held, **Then** the button toggles at the
   configured rate.
4. **Given** a gamepad is unplugged during play, **Then** the game pauses
   (a setting, on by default) and a message says which player lost its
   controller.
5. **Given** the "allow opposite directions" setting is off (the default),
   **Then** Left+Right and Up+Down are never sent together, as on real
   controllers.

---

### User Story 6 - The AI can do everything the user can (Priority: P2)

As a user who works with an AI assistant, I can turn on the API and the "MCP
server" in Settings. An AI client can then run every menu action and setting
through `gui.*` commands, on the same session I am playing. I can always see
when automation is connected or has paused the game, and I can take control
back.

**Why this priority**: Automation parity is constitutional (Principle V).
This story also delivers feature 002's GUI-mode acceptance (US5, SC-006,
FR-009, FR-020, FR-021(b)), which #32 tracks.

**Independent Test**:
1. Enable the API and the MCP server in the GUI.
2. Attach a stdio client with `cutenes-server --attach`.
3. List the tools and check that every menu action has a `gui.*` command.
4. Drive the game through MCP while a human presses buttons, and check that
   both sides see the same state at every step.

**Acceptance Scenarios**:

1. **Given** a first launch, **Then** the API and the MCP server are both off
   and no network port is open.
2. **Given** the user turns on the MCP server, **Then** the API turns on with
   it (MCP requires the API), the MCP token file is written, and Settings
   shows the address and a "Copy token" button.
3. **Given** any menu action, dialog or setting, **Then** a `gui.*` command
   performs it and reports its result. Each command has a schema and appears
   in `api.catalog` and `tools/list`.
4. **Given** automation is connected, **Then** the status bar shows an
   indicator. **When** automation pauses the game, **Then** the indicator
   says so, and the user can resume.
5. **Given** clients are attached, **When** the user turns off the API,
   **Then** their calls in progress fail with `ShuttingDown`, the streams
   close, and the game keeps running.
6. **Given** a human and an AI change state alternately (input, save/load,
   reset, memory writes), **Then** neither ever observes a torn or
   inconsistent machine state (002 SC-006).
7. **Given** the save-state slots, **Then** the slots that automation sees
   are the same slots the menus show.

---

### User Story 7 - Display, sound and comfort settings (Priority: P3)

As a player, I can choose the window scale, fullscreen, pixel aspect ratio,
overscan cropping, a smoothing or scanline filter and a palette; adjust the
volume and audio latency; and set how the emulator behaves when its window
loses focus.

**Why this priority**: These make the picture and sound right for each
player's screen and taste. Sensible defaults ship with P1, and the dialogs
come here.

**Independent Test**: Change each setting, restart, and confirm that it
persisted and took effect. Measure the image size at each scale and aspect
setting.

**Acceptance Scenarios**:

1. **Given** scale 1x-6x, **Then** the window's video area is that multiple
   of the picture (after aspect correction and cropping). **Given**
   fullscreen, **Then** the picture fills the screen at the chosen aspect,
   using integer multiples when "integer scaling" is on.
2. **Given** the aspect setting, **Then** the options are square pixels,
   8:7 pixel aspect (NTSC hardware) and 4:3 stretch.
3. **Given** overscan cropping, **Then** the top, bottom, left and right
   crops are each adjustable. The default hides the top and bottom 8 lines
   for NTSC, as most TVs did.
4. **Given** "pause when inactive" is on (the default), **When** the window
   loses focus, **Then** the game pauses, and it resumes when focus returns.
5. **Given** the audio device changes or disappears, **Then** sound moves to
   the new default device, or the game runs silently with a message. It
   never crashes or stalls.

---

### User Story 8 - Cheats and movies (Priority: P3)

As a player, I can find and apply cheats (search RAM for a value that
changes, then freeze or set it; or enter Game Genie codes), and record my
inputs as a movie that plays back exactly. Like Cheat Engine, the search
works while I play: I enter the value I see (lives: 3), play until it
changes, enter the new value, and repeat until one address is left; then I
change it, and the game shows the change at once.

**Why this priority**: These are standard emulator features named in the
constitution. They rely on determinism, which the core already guarantees.

**Independent Test**:
- Record a 30-second movie from power-on.
- Play it back and confirm that the frame hash at the end matches the
  recording.
- On a test ROM, use the cheat search to find a RAM counter and freeze it.

**Acceptance Scenarios**:

1. **Given** the cheat search, **When** the user narrows candidates by
   "equal, not equal, greater, less, changed, unchanged" between snapshots,
   or by an exact value, **Then** the remaining addresses and their values
   are listed, and any of them can be added as a cheat.
2. **Given** the cheat search window is open, **Then** the game keeps
   running and taking input, and the listed values refresh live (at least
   10 times a second). Each snapshot is taken at a frame boundary.
3. **Given** a listed or watched address, **When** the user types a new
   value, **Then** it is written at the next frame boundary and the game
   shows its effect without a pause. Addresses can be kept in a watch list
   with live values and labels, saved per game.
4. **Given** an enabled cheat, **Then** it is applied every frame. Cheats are
   saved per game in the library, and each can be switched on or off.
5. **Given** a valid Game Genie code, **When** it is enabled, **Then** the
   game behaves as with the original accessory. An invalid code is refused
   with the reason.
6. **Given** movie recording from power-on or from a save state, **When** the
   movie is played back, **Then** each frame's input is identical and the
   final frame hash equals the recording's.
7. **Given** a movie is playing in read-only mode, **Then** the user's input
   is ignored until playback ends or the user takes over (read-write), which
   continues recording from that frame.

---

### User Story 9 - Debugging tools (Priority: P3)

As a homebrew developer or curious player, I can pause and inspect the
machine: CPU registers and disassembly with stepping and execution
breakpoints, a memory viewer and editor, pattern tables, nametables,
sprites and palettes, a trace log, and a full memory dump.

**Why this priority**: These views make cuteNES useful for development and
for checking emulator accuracy. The core exposes all the data they need.

**Independent Test**: Load nestest, set an execution breakpoint at `$C000`,
power on, and check that it stops there. Step 10 instructions and compare
the registers with nestest's reference log. Open the PPU viewer on a test ROM
and compare the pattern table with the CHR data.

**Acceptance Scenarios**:

1. **Given** the debugger, **When** the game is paused, **Then** it shows the
   CPU registers, the flags and a disassembly around PC. Step Instruction,
   Step Frame and Run work. Registers can be edited.
2. **Given** an execution breakpoint at an address, **When** the CPU reaches
   it, **Then** emulation pauses there and the debugger shows it.
3. **Given** the memory viewer, **Then** CPU memory, PPU memory, OAM and
   palette RAM can be shown in hex and edited. Reads in the viewer never
   change the game's state (side-effect-free).
4. **Given** the PPU viewer, **Then** the pattern tables (with a selectable
   palette), the four nametables (with the scroll position marked), the 64
   sprites and the palette are shown and refresh while the game runs.
5. **Given** Debug > Write Memory Dump, **Then** a dump file is written to
   the library's debug folder and its name is shown.

---

### User Story 10 - Updates when I want them (Priority: P3)

As a player, I can let cuteNES tell me when a new version is out and
install it for me, and if I never opt in it never goes online.

**Why this priority**: Most users never visit a releases page, so fixes do
not reach them. It is opt-in because an emulator has no other reason to use
the network.

**Independent Test**: Point the update check at a local fake release feed
with a newer version for this platform. Launch with no game: the prompt
appears, and Install replaces a copy of the application, which restarts with
the same arguments. Launch with a ROM argument: no prompt.

**Acceptance Scenarios**:

1. **Given** the first launch, **Then** cuteNES asks whether to check for
   updates, and nothing is requested from the network unless the user says
   yes. The answer can be changed later in Settings.
2. **Given** update checks on and a newer release with a build for this
   platform, **When** cuteNES launches and no game is running, **Then** it
   shows the new version with Install, Ask Again Later, Skip This Version and
   See Details.
3. **Given** a game started from the command line, or started before the
   check finishes, **Then** no prompt appears on that launch.
4. **Given** Install, **Then** the build is downloaded and verified, replaces
   the current version, and cuteNES quits and starts the new version with the
   same command-line arguments.
5. **Given** Skip This Version, **Then** that version is never offered again,
   and a newer one is. **Given** Ask Again Later, **Then** the next launch
   asks again. **Given** See Details, **Then** the release's GitHub page
   opens in the browser.

---

### Edge Cases

- **Same ROM content in several files.** Both are listed, and they share one
  set of saves (the game is identified by content).
- **Same file name, different content.** Each version keeps its own saves.
- **A ROM deleted or moved while it is running.** The game keeps running
  from memory, and saves still go to the library. Recent entries whose file
  is missing are marked as missing.
- **A large or slow library** (thousands of ROMs, a network drive). The
  browser appears at once from the last index and updates as the scan
  proceeds. A ROM added to or removed from the folder appears or disappears
  without a restart.
- **A corrupt battery save.** It is set aside (renamed, not deleted), the
  game starts with fresh RAM, and the user is told.
- **A second instance of cuteNES.** It runs normally. If the first instance
  already serves the API or MCP port, the second reports "port in use" and
  leaves its toggle off. Two instances must not corrupt each other's saves:
  a save in use by another instance is not overwritten without warning.
- **Closing the window during an AI-driven run.** In-flight automation calls
  fail with `ShuttingDown`, the battery save is written, and then the
  application exits.
- **Loading a state for a different game.** It is refused (the core's
  `WrongCartridge`), and the game keeps running.
- **A movie that does not match the loaded game.** It is refused before
  playback starts.
- **The display is changed while fullscreen** (a monitor is unplugged, or the
  DPI changes). The window moves to an available screen and rescales.
- **A gamepad and the keyboard pressed together for one player.** The inputs
  are combined.
- **A hotkey that is also a game binding.** It is reported as a conflict, and
  the hotkey wins only if the user confirms.
- **The library folder is on a read-only medium.** The library browser still
  lists ROMs. Saving reports the error and offers Settings > Library.
- **An update download fails, is cut short or does not verify.** Nothing is
  replaced; the current version keeps running, and the user is told.
- **cuteNES cannot write where it is installed** (a system folder, a
  distro package). The prompt offers See Details instead of Install.
- **Offline, or GitHub unreachable.** The check fails quietly; launch is
  never delayed or blocked by it.

## Requirements *(mandatory)*

### Functional Requirements

**Launch and discovery**

- **FR-001**: The application MUST accept an optional ROM path on its command
  line. With one, it boots that ROM directly. Without one, it shows the
  library browser. It MUST also accept options for fullscreen, a library
  location override, loading a state slot at start, and enabling the API or
  MCP server for that run; `--help` and `--version` MUST be provided.
- **FR-002**: The library browser MUST list every ROM found under the
  library's ROM folder and under any ROM folders the user added (Settings >
  Library), including subfolders, plus recently opened ROMs from any other
  location. Added folders are scanned in place and never written to; the
  user can remove them, but not the library's own ROM folder. For each it MUST show the name, the board/mapper,
  region, whether it is supported, whether a battery save or states exist,
  and when it was last played.
- **FR-003**: The library browser MUST support filtering by typed text,
  sorting (name, last played), and a "Recent" group. It MUST be fully
  operable with the mouse, with the keyboard alone, and with a gamepad
  alone.
- **FR-004**: ROMs MUST be openable from any location by File > Open ROM, by
  dragging a file onto the window, from the command line, or through the
  operating system's "open with". The library is for quick discovery only.
- **FR-005**: The library index MUST appear immediately from its last
  contents and refresh in the background. Changes to the ROM folders MUST be
  picked up without restarting.
- **FR-006**: A ROM the core rejects MUST be listed as unsupported with the
  core's reason. Opening it MUST show that reason and leave any running game
  unaffected.

**Library folder**

- **FR-010**: The library folder MUST hold the user's data in visible,
  documented subfolders: `roms` (the user's ROMs), `saves` (battery saves),
  `states` (save-state slots and their thumbnails), `screenshots`, `movies`,
  `recordings`, `cheats` and `debug` (memory dumps). It MUST be created at
  first launch at a default per-user location, and its location MUST be
  changeable in Settings.
- **FR-011**: Per-game data MUST be identified by the ROM's content, so that
  renaming or moving a ROM keeps its saves. File names MUST still be human
  readable (they include the game's name).
- **FR-012**: Battery saves MUST be written when the game is closed, replaced
  or the application exits, and within 10 seconds of the game changing its
  battery RAM. A write MUST be atomic: an interrupted write never leaves a
  corrupted save in place.
- **FR-013**: Battery saves MUST be importable and exportable as raw `.sav`
  files, the format other emulators use.
- **FR-014**: Application settings MUST persist across runs in the platform's
  standard per-user settings location, separate from the library, so that
  the library can be shared or synced on its own.

**Menus and actions**

- **FR-020**: The menu bar MUST give access to every feature. The menus MUST
  contain at least the following items. Shortcuts are listed where they are
  conventional; the full default table is a planning artifact.
  - **File**: Open ROM… (Ctrl+O), Open Recent ▸ (with Clear), Game Library
    (Ctrl+L), Close Game; Save State ▸ slots 1-10, Load State ▸ slots 1-10,
    Quick Save (F5), Quick Load (F7), Select Slot ▸, Undo Load State, Save
    State to File…, Load State from File…; Import Battery Save…, Export
    Battery Save…; Take Screenshot (F12); Open Library Folder; Exit.
  - **Emulation**: Pause/Resume, Reset, Power Cycle, Frame Advance, Fast
    Forward (hold and toggle), Rewind (hold), Speed ▸ (25%-400% and
    unlimited), Region ▸ (Automatic, NTSC, PAL).
  - **View**: Scale ▸ (1x-6x), Fullscreen, Integer Scaling, Aspect Ratio ▸,
    Overscan ▸, Filter ▸ (Nearest, Smooth, Scanlines), Palette ▸, Show FPS,
    Show On-Screen Messages, Always on Top.
  - **Tools**: Cheats…, Cheat Search…, Movie ▸ (Record…, Play…, Stop,
    Read-Only), Record Audio… / Stop Recording.
  - **Debug**: Debugger, Memory Viewer, PPU Viewer ▸ (Pattern Tables,
    Nametables, Sprites, Palette), Trace Logger, Write Memory Dump.
  - **Settings**: Video…, Audio…, Input…, Hotkeys…, Emulation…, Library…,
    Servers (API and MCP server)….
  - **Help**: Keyboard Shortcuts, User Guide, Open Log Folder, About cuteNES
    (with the versions of the application, core and API surface, and the
    third-party licenses, including the Qt LGPL notice).
- **FR-021**: Actions that need a loaded game MUST be disabled, not hidden,
  when none is loaded. Every action with a shortcut MUST show it in its menu.
- **FR-022**: Every hotkey MUST be rebindable to a key or a gamepad control.
  Bindings MUST be checked for conflicts with each other and with game
  input.
- **FR-023**: Each action MUST give visible feedback: an on-screen message
  for quick actions (state saved, speed changed) and a dialog for errors.
  On-screen messages MUST be switchable off.
- **FR-024**: The menus MUST follow each platform's conventions: the
  application menu on macOS, native file dialogs, and the system light/dark
  theme.

**Play**

- **FR-030**: Games MUST run at the console's native rate (60.0988 fps NTSC,
  50.0070 fps PAL) with continuous audio. Pacing MUST keep audio free of
  underruns while video stays smooth, and the user MUST be able to trade
  latency for robustness with an audio buffer setting.
- **FR-031**: Input MUST reach the game in the next frame the game polls
  after the key or button event. The application MUST NOT add a frame of
  buffering of its own.
- **FR-032**: Keyboard and gamepad input MUST be supported for two
  players, with defaults per US5, remapping, turbo A/B at a configurable
  rate, gamepad hot-plug, and an "allow opposite directions" option (off by
  default).
- **FR-033**: Ten save-state slots per game, with thumbnails and times,
  quick save/load of the selected slot, and Undo Load State MUST be
  provided. States MUST also be savable to and loadable from any file.
- **FR-034**: Rewind MUST be available with at least 30 seconds of history at
  default settings. The length and memory use MUST be configurable, and
  rewind MUST be switchable off.
- **FR-035**: Fast-forward (hold or toggle) and speed settings from 25% to
  400% plus "unlimited" MUST be provided, with audio pitch-shifted or muted
  as configured. Frame Advance MUST run exactly one frame.
- **FR-036**: The application MUST optionally pause when its window loses
  focus (on by default) and when a gamepad in use is disconnected (on by
  default). It MUST hide the pointer over the game during play.
- **FR-037**: The application MUST be able to resume where the user left off:
  an optional automatic state on close, offered when the game is next
  opened (off by default).

**Display and sound**

- **FR-040**: Display settings MUST include a window scale of 1x-6x,
  fullscreen (borderless, on the current screen), integer scaling, aspect
  ratio (square pixels, 8:7 pixel aspect, 4:3), per-edge overscan cropping
  (default: 8 lines top and bottom for NTSC), and filters (nearest, smooth,
  scanlines). They MUST render correctly on high-DPI screens.
- **FR-041**: Palette selection MUST offer the default palette, the
  alternatives the project documents, and loading a standard 64-color
  (192-byte) `.pal` file. This needs a core addition (see "Core additions").
- **FR-042**: Audio settings MUST include the output device, the volume, mute,
  and the latency/buffer size. Audio device changes MUST never stop
  emulation.
- **FR-043**: Screenshots MUST be saved as lossless PNG at the console's
  native resolution, without filters or scaling, into the library's
  `screenshots` folder.
- **FR-044**: Audio recording MUST write an uncompressed WAV of the game's
  output into the library's `recordings` folder.

**Cheats and movies**

- **FR-050**: Cheat search MUST snapshot CPU RAM ($0000-$07FF, plus
  cartridge RAM when present) and narrow candidates by comparison with the
  previous snapshot or a value (equal, not equal, greater, less, changed,
  unchanged). Values MAY be 1 byte or 2 bytes (little-endian), unsigned. The
  search MUST work while the game runs (US8-2).
- **FR-053**: Search results and a per-game watch list MUST show live
  values and MUST let the user edit a value in place. The write happens at
  the next frame boundary, through the same session queue as every other
  change (FR-075), so a running game shows it at once (US8-3).
- **FR-051**: Cheats MUST be stored per game in the library. Each MUST be
  switchable on or off, and the forms MUST be: freeze an address to a value,
  set an address once, and Game Genie codes (6 and 8 letters). Game Genie
  needs a core addition.
- **FR-052**: Movies MUST record controller input per frame, starting from
  power-on or from an embedded save state. They MUST play back
  deterministically, and play read-only with an explicit takeover to
  read-write. A movie MUST record the ROM's identity and refuse to play on a
  different game.

**Debugging**

- **FR-060**: The debugger MUST show the CPU registers, flags and a
  disassembly around PC. It MUST support Step Instruction, Step Frame, Run,
  execution breakpoints and register editing.
- **FR-061**: The memory viewer/editor MUST cover CPU memory, PPU memory, OAM
  and palette RAM. Viewing MUST be side-effect-free.
- **FR-062**: The PPU viewer MUST show the pattern tables with a selectable
  palette, the nametables with the scroll position, all 64 sprites, and the
  palette RAM. It MUST refresh live.
- **FR-063**: The trace logger MUST log executed instructions in the
  project's trace format to a file in the library's `debug` folder.
- **FR-064**: Debug > Write Memory Dump MUST write the core's debug dump into
  the library's `debug` folder (the "dump" feature kept from 002).

**Automation (constitution Principle V; feature 002 FR-009, FR-020, FR-021(b), FR-023)**

- **FR-070**: Every menu action, dialog and setting MUST have a `gui.*`
  command named `gui.verb_object`, with input and output schemas,
  registered through the `cutenes-api` host extension point. An automated
  audit MUST show a 1:1 mapping between the menu inventory (FR-020) and the
  `gui.*` catalog.
- **FR-071**: Settings > Servers MUST hold two toggles, "API" and "MCP
  server", both off by default, each with a port. MCP requires the API.
  Turning MCP on MUST write the MCP token file that `cutenes-server
  --attach` reads. The token MUST be viewable and copyable.
- **FR-072**: Turning the API off MUST end client calls with `ShuttingDown`
  and close streams, while emulation continues.
- **FR-073**: The status bar MUST show when automation clients are
  connected and when automation has paused the game. The user MUST always
  be able to resume or turn the servers off.
- **FR-074**: In GUI mode, the save-state slots that the API's `session.*`
  slot commands reach MUST be the same persistent slots that the menus show,
  and `session.library_list` MUST list the library browser's ROMs.
- **FR-075**: All state changes from the GUI and from automation MUST go
  through the one session queue (002 FR-007). Neither side may observe a
  torn state.

**Platform and architecture**

- **FR-080**: The application MUST build and run on Windows, macOS and Linux
  from one codebase, using Qt 6 dynamically linked under LGPL-3.0, and MUST
  use only the core's public API (constitution II, IV).
- **FR-081**: The emulation view MUST be a reusable component that other Qt
  applications can embed, per `specs/001-emulation-core/ui-embeddable-design.md`.
- **FR-082**: Every dialog and the library browser MUST be operable with the
  keyboard alone, and controls MUST carry accessible names for screen
  readers.

**Updates (#105)**

- **FR-090**: Update checks MUST be off until the user turns them on. The
  first launch MUST ask once, and Settings MUST let the user change the
  answer. With them off, the application MUST make no network request for
  updates.
- **FR-091**: With checks on, the application MUST check once per launch,
  in the background, without delaying the window or play. It MUST ask only
  when no game is running; a game started from the command line or before
  the check finishes suppresses the prompt for that launch.
- **FR-092**: The check MUST look at this project's GitHub Releases for the
  newest non-prerelease version above the running one that has a build for
  the running OS, architecture and package type, and nothing else (no
  telemetry, no identifiers beyond what an HTTPS request carries).
- **FR-093**: The prompt MUST show the new version and offer Install, Ask
  Again Later, Skip This Version and See Details. Skip MUST be remembered per
  version; See Details MUST open the release page in the browser.
- **FR-094**: Install MUST download over HTTPS and verify the build against
  a checksum published with the release before replacing anything. Any
  failure MUST leave the installed version untouched.
- **FR-095**: After a verified download, the new version MUST replace the
  current one in place, and the application MUST quit (with the usual
  battery save) and start the new version with the same command-line
  arguments.
- **FR-096**: Where the application cannot replace itself (no write access,
  a package-manager install), the prompt MUST offer See Details instead of
  Install.
- **FR-097**: The update setting MUST be readable and settable through
  `gui.get_settings`/`gui.set_setting` (FR-070). No automation command may
  install an update: replacing the application needs the user's click.

### Core additions this feature needs

These menu features need core capabilities the core API does not have yet.
Each one is a core API amendment, specified and tested test-first in feature
001 before the GUI uses it:

- **Region override** (Emulation > Region): force NTSC or PAL, overriding the
  header.
- **Palette selection** (View > Palette, FR-041): a palette applied by the
  core's colour output, or palette-index frames that the consumer colours.
- **Game Genie** (FR-051): ROM-read substitution with the accessory's
  compare semantics.

The API surface gains the matching `core.*` commands, so that automation
keeps parity.

### Key Entities

- **Library**: the user's folder, with the subfolders of FR-010. It has a
  location, a ROM index (path, content identity, header info, supported or
  not) and recent entries outside the library.
- **Game identity**: the ROM's content checksum plus a display name. All
  per-game data hangs off it.
- **Battery save**: the raw cartridge RAM for one game.
- **Save-state slot**: a numbered (1-10) or named state for one game, with a
  thumbnail, a time and the core version that wrote it.
- **Movie**: the game identity, a start point (power-on or an embedded
  state) and per-frame input for two ports.
- **Cheat**: a game identity, a kind (freeze, set, Game Genie), an address or
  code, a value, a label and an enabled flag.
- **Settings**: video, audio, input bindings, hotkeys, emulation, library
  location, servers. Stored per user, outside the library.
- **GUI command**: one `gui.*` entry per menu action or setting (FR-070).
- **Update preference**: asked or not, on or off, and the skipped versions.
- **Release**: a version, its page and one build per platform, each with a
  checksum.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: From launching without arguments, a user reaches gameplay of a
  library ROM in 3 interactions or fewer (for example: select, confirm),
  using either a keyboard or a gamepad alone.
- **SC-002**: With 2,000 ROMs in the library, the browser is usable within 1
  second of launch (from the saved index), and a full rescan completes in
  the background without blocking play.
- **SC-003**: The menu audit finds 100% of the FR-020 actions present and
  working, and a 1:1 mapping between menu actions and `gui.*` commands.
- **SC-004**: During a 10-minute run on reference hardware, the game runs at
  its native frame rate with no audio underruns and no dropped or repeated
  frames outside deliberate speed changes.
- **SC-005**: Input reaches the game within one frame of the event in 100%
  of measured presses (FR-031).
- **SC-006**: After a forced kill of the application during play, the battery
  save on disk is at most 10 seconds behind the game, and no save file is
  ever corrupted.
- **SC-007**: Saving and loading slot N returns the identical frame hash and
  machine state in 100% of tests. A movie replays to the identical final
  frame hash in 100% of tests.
- **SC-008**: Every scenario of feature 002's SC-006 (interleaved human and
  AI state changes) passes in GUI mode.
- **SC-009**: The application builds and its automated GUI tests pass on
  Windows, macOS and Linux in CI.
- **SC-010**: With update checks off, a full session makes no network
  request for updates. With them on, an update installed from a test
  release restarts as the new version with the same arguments on Windows,
  macOS and Linux, and a corrupted download leaves the old version working.

## Assumptions

- **Default library location**: a `cuteNES` folder in the user's Documents
  folder, so it is visible and easy to back up. Settings live in the
  platform's per-user settings location. A portable mode (settings and
  library next to the executable) is out of scope for 1.0.
- **ROM files**: `.nes` (iNES 1.0 and NES 2.0) only. Compressed archives
  (`.zip`, `.7z`), Famicom Disk System, NSF and UNIF are out of scope, as for
  the core.
- **Game identity**: the content checksum excludes the header, so that a
  re-headered dump keeps its saves.
- **No network features** apart from opt-in update checks (US10): no
  box-art scraping, netplay or achievements. Thumbnails come from the user's own screenshots and states.
- **Accessories**: only the two standard controllers, as in the core
  (Zapper, Four Score and others are out of scope).
- **Video recording**: needs an encoder dependency and is out of scope.
  Audio recording (WAV) and screenshots (PNG) are in scope.
- **Not in scope**: NTSC composite filters, shader packs, run-ahead and
  per-channel audio volumes. They may become later features.
- **Debugging**: memory read/write breakpoints need core hooks and are out of
  scope. Execution breakpoints use the core's instruction trace.
- **One instance, one session**: each window is one emulator session, as in
  feature 002 ("One session per process").
- **Save states**: the core's format is not stable before 1.0, and states
  from another core version may be refused (FR-033, edge cases).
- **Hotkey defaults**: they follow common emulator conventions. The
  complete default table is a planning artifact (contract), not part of this
  spec.
