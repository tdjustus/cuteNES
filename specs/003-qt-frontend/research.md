# Research: Qt desktop frontend

**Feature**: 003 | **Date**: 2026-10-31

Each entry gives the decision, why it was chosen, and what was rejected.

## R1. Qt version, modules and licensing

**Decision.**
- Qt **6.8 LTS or later**.
- Modules:
  - Widgets
  - Gui (QRhi, `QRhiWidget` since 6.7)
  - Concurrent (library scan)
  - ShaderTools (`qt_add_shaders`)
  - Svg (icons)
- CI installs Qt 6.8 with `jurplel/install-qt-action@v4.4.1`, pinned
  (released 2026-09-20).
- Development machines may use newer releases; the maintainer's Homebrew Qt
  is 6.12.
- Qt is linked dynamically under LGPL-3.0. About shows the Qt, SDL3 and
  project licenses, and packages ship the LGPL text and the Qt license files.

**Why.**
- 6.8 is the current LTS line, and it has `QRhiWidget` and the QRhi APIs this
  plan uses.
- The Shader Tools runtime is LGPL-3.0 or GPL-2.0.
- The `qsb` compiler is GPL-3.0, but it runs only at build time, and its
  output (the shader packs) is not a derivative of `qsb`.
- Dynamic linking satisfies the constitution's licensing constraint.

**Rejected.**
- Qt 6.5: older, and it lacks `QRhiWidget`.
- QML/Qt Quick for the main UI: menus, docks and dialogs are a widgets
  application's strength, and a QML wrapper for the view remains possible
  (`ui-embeddable-design.md`).

## R2. Rendering

**Decision.** `EmulationView` is a `QRhiWidget`:
- It uses Metal on macOS, D3D11 on Windows, and Vulkan or OpenGL on Linux.
- Each frame, the 256x240 XRGB8888 picture is uploaded to a texture, then
  drawn with:
  - viewport math for scale, aspect (1:1, 8:7 or 4:3) and overscan crop;
  - a sampler that is nearest or linear (the "Nearest" and "Smooth" filters);
  - a small fragment shader for "Scanlines".
- The widget presents on the display's vsync.
- If QRhi cannot initialise, the view falls back to a raster `QWidget` that
  paints a `QImage` with `QPainter`. The fallback supports the same scaling
  and aspect options, draws scanlines as a painted overlay, and is not
  vsync-locked.

**Why.**
- QRhi is Qt's own cross-API layer, so it needs no platform code.
- Vsync-locked presentation is what lets display-synced pacing (R4) avoid
  dropped frames.
- At this resolution the upload is negligible.

**Rejected.**
- `QOpenGLWidget`: OpenGL is deprecated on macOS.
- `QPainter` alone: it tears and cannot sync to the display.

## R3. Real-time mode in `cutenes-api`

**Decision** (as built in G1). The session gets a real-time mode, off by
default, so headless behavior does not change:

```cpp
void Session::setRealtime(bool on);
void Session::requestFrames(std::uint32_t frames = 1);   // any thread
void Session::setLiveInput(Port port, ButtonSet buttons); // any thread, lock-free
void Session::setMediaSinks(IVideoSink*, IAudioSink*);    // host's sinks
```

- **The clock stays in the host.** The host's clock (a timer, or the
  display's vsync) calls `requestFrames`.
- **The emulation thread loop.** Queued commands run first. Then, while
  real-time is on and the machine is `Running`, one frame runs per pending
  request, and the `frame` event is published.
- **Pending frames.**
  - Requests made while the machine is not `Running` are dropped.
  - At most twice the latest request stays pending, so a stall never turns
    into a burst.
- **Input.**
  - At each real-time frame start, a port's input is the bitwise OR of the
    live human input and the last `core.set_controller` value. Both
    surfaces can press buttons, and neither silently erases the other.
  - A scripted `core.run_frames` is exact. The script alone decides its
    frames (002 FR-019).
- **Media.** The host's sinks receive every frame and audio chunk after the
  session's own capture, so `core.screen` and `core.audio` keep working.
- **Pause.**
  - The session's `pause` and `resume` set the run state, so the GUI and
    automation see the same state.
  - `pausedByAutomation` keeps its 002 meaning: the status bar shows when
    automation paused the game (FR-073).

**Why.**
- It keeps one queue and one thread (002 R5, FR-075).
- Commands run between frames, never mid-frame, so neither side sees a torn
  state (SC-008).
- A host-driven clock lets G1 use a timer and G5 add vsync without
  changing the API.

**Rejected.**
- An `IFrameClock` that the session waits on: it puts the clock policy in
  the API and makes waking for queued commands awkward.
- The GUI owning its own `Machine`: it would bypass the session queue.
- Driving frames from the GUI thread through `core.run_frames`: a round trip
  per frame adds latency and jitter.

## R4. Pacing and audio

**Decision.**
- **SDL3 audio stream.** The core's samples (48 kHz mono `int16`) are put on
  an `SDL_AudioStream` bound to the default device, or the chosen one.
- **Dynamic rate control.** Every frame, the queued amount is compared with
  the target latency (default 64 ms, adjustable from 32 to 200). The stream's
  frequency ratio is nudged by at most ±0.5% (`SDL_SetAudioStreamFrequencyRatio`).
- **Pacing.** G1 ships the timer mode. The display-synced mode arrives with
  the video settings in G5. `FrameClock` chooses between two modes:
  - **Sync to display** (the default when the display's refresh rate is
    within 1% of the console rate, or of an integer multiple of it): a frame
    is due on each vsync, or on every Nth at 120/144 Hz. Dynamic rate control
    absorbs the 0.16% difference, so no frames are dropped or repeated.
  - **Sync to audio** (other refresh rates, or VRR off): a high-resolution
    timer runs at the console rate, and video shows the newest frame at each
    vsync.
- **Speed and fast-forward.**
  - Speed scales the clock.
  - Audio is time-stretched by the frequency ratio up to 2x and muted above
    that. A setting can also mute it during fast-forward.
  - "Unlimited" runs frames back to back and presents at vsync.
- **Device loss.** SDL reports device changes. The stream rebinds to the new
  default device, or the game runs silently with an on-screen message. The
  clock falls back to the timer, so emulation never stalls (US7).

**Why.** This is the established pairing of vsync with dynamic rate control:
it gives smooth video and gap-free audio together (SC-004). SDL3's stream
supplies the resampler.

**Rejected.**
- Audio-master pacing alone: it drops or repeats a video frame about every 10
  seconds at 60 Hz, which breaks SC-004.
- Qt Multimedia: it has no rate control (plan "Complexity").

## R5. Input

**Decision.**
- **Keyboard.** Key events on the main window, and on `EmulationView` while
  it has focus, map through `InputMapper` to a `ButtonSet` per port.
  `Session::setLiveInput` receives it. Key releases on focus loss clear the
  set, so no key gets stuck.
- **Gamepads.** SDL3 `SDL_Gamepad` (its controller database gives standard
  layouts), polled on the GUI thread at 1 kHz from a timer, with hot-plug
  events. The first gamepad goes to player 1 and the second to player 2.
  Unplugging one pauses the game when that setting is on.
- **Turbo.** Turbo A/B toggle on a frame counter at the configured rate (2-30
  presses per second; default 15).
- **Opposite directions.** When this is off, the most recent of Left/Right
  (and of Up/Down) wins.
- **Latency.** Live input is read at each frame start (R3). A press reaches
  the game at the next frame, within one frame of the event (SC-005).

**Rejected.** QtGamepad: it was not ported to Qt 6.

## R6. Rewind and save states

**Decision.**
- **Rewind** captures a state every frame into a ring buffer. Each
  sixtieth state is a keyframe. Every other state is stored as an XOR
  against the previous state, run-length encoded, because most bytes do not
  change between frames.
- The default budget is 64 MiB, which holds at least 30 s (FR-034). G3
  measures the real number and adjusts the budget.
- Rewinding plays the ring backwards, one state per displayed frame. While
  it does, audio is muted.
- **Slots** are files in `states/` with a PNG thumbnail
  ([library-layout.md](contracts/library-layout.md)).
- **Undo Load State** keeps the state captured just before each load.

**Why.** Delta and RLE are simple, deterministic and fast enough to run every
frame. The core's states are already byte-identical round trips.

**Rejected.** Snapshots every N frames with re-emulation: it complicates
audio, and it is slower to rewind.

## R7. Game identity and file names

**Decision.**
- **Content identity.** The identity is the SHA-1 of the ROM data after the
  16-byte header and any trainer. That is the headerless hash that No-Intro
  uses, so re-headered dumps share saves.
- **File names.** Per-game files are named `<display name> [<first 8 hex of
  SHA-1>].<ext>`, and lookup matches on the bracketed hash. The display name
  is the ROM file's stem when the game was first seen. Renaming the ROM
  later keeps the old file names, which still match.

**Why.** The names stay human readable (FR-011), and the hash keeps
identically named but different ROMs apart.

## R8. Persistence and the library

**Decision.**
- **Settings.** `QSettings` in INI format, at
  `QStandardPaths::AppConfigLocation`, separate from the library (FR-014).
- **Library location.** The default is
  `QStandardPaths::DocumentsLocation/cuteNES`.
- **Library index.** A JSON cache at `QStandardPaths::CacheLocation`.
  - Each entry holds the path, size, mtime, identity, header info and
    support status.
  - On launch the browser shows the cache immediately. A `QtConcurrent` scan
    then updates entries whose size or mtime changed.
  - A `QFileSystemWatcher` on the `roms` tree triggers rescans (FR-005).
- **Support status.** Each ROM's status comes from loading it on a scratch
  `Machine` and reading its `LoadResult` (FR-006).
- **Battery saves.**
  - The emulation thread hashes `sram()` every 60 frames.
  - When the hash changes, the bytes go to a writer thread, which writes at
    most once per 5 s and always on close (FR-012: within 10 s).
  - Writes use `QSaveFile` (temp file, then rename), so they are atomic.
  - A corrupt or wrong-size save is renamed to `.bad-<date>`.
- **Concurrent instances.** A `QLockFile` per game in `saves/` stops two
  instances writing the same save. The second instance warns and plays
  without saving the battery file.

## R9. Debugger and breakpoints

**Decision.**
- The views read through side-effect-free `core.*` commands (`peek`,
  `read_vram`, `read_oam`, `read_palette`, `get_cpu`, `disassemble`),
  batched into one queued command per refresh, at up to 30 Hz.
- **Execution breakpoints.** While at least one is set, real-time frames run
  as `stepInstruction` calls up to the frame end, checking PC against the
  breakpoint set. Without breakpoints, frames run at full speed.
- **Trace logger.** It uses `setInstructionTrace`, writing to a file through
  a bounded queue.
- **Step Frame** is `frameAdvance`.

**Why.** Stepping is exact and needs no core change. A test measures the
slowdown with breakpoints set. Full speed is not required while debugging.

## R10. Core additions (feature 001 amendments)

**Decision.** See [contracts/core-additions.md](contracts/core-additions.md):
- **Region override**: `setRegionOverride(std::optional<Region>)`, applied
  at the next load or power-on. Region timing per the nesdev "Clock rate" and
  "Cycle reference chart" pages.
- **Palette**: `setPalette(std::span<const std::uint32_t>)` with 64 entries
  (emphasis applied as now) or 512 entries (emphasis included). An empty
  span restores the default. The `.pal` format follows the nesdev
  "PPU palettes" page.
- **Game Genie**: `setGameGenieCodes(std::span<const GameGenieCode>)`. The
  core decodes 6- and 8-letter codes, following the nesdev "Game Genie" page.
  CPU reads in `$8000-$FFFF` are substituted (with the compare value for
  8-letter codes), as the accessory did.

Each addition gets a `core.*` command and a `core-api.md` capability row
(automation parity). All three are added test-first.

**Why.** The decoding and substitution rules are hardware documented. Each
needs the core, because the GUI sees only RGB frames and cannot intercept
cartridge reads.

## R11. GUI commands and thread marshalling

**Decision.**
- **Actions.** Every menu item is a `QAction` with a `command` property, its
  `gui.*` name. Triggering the action calls the session's `gui.*` command,
  so a user's click and an AI's call run the same code.
- **Handlers.** They run on the emulation thread (002 R7).
  - Machine work runs inline.
  - Widget work is posted to the GUI thread with `QMetaObject::invokeMethod`.
    Most widget work uses a queued connection. `BlockingQueuedConnection` is
    used only when the result depends on the GUI, for example a dialog's
    state.
- **Modal dialogs.** A blocking dialog never runs inside a handler: commands
  that open a dialog return as soon as it is shown. The dialog's own
  commands (`gui.set_*`, `gui.close_dialog`) drive it.
- **Settings.** Each setting has a `gui.get_settings` / `gui.set_setting`
  path, and the setting's key is in its schema (an enum per group).
- **Audit.** `gui-audit` walks the menu bar and the settings registry and
  checks that the action set equals the `gui.*` catalog (FR-070, SC-003).

## R12. Testing

**Decision.**
- **Automated.**
  - Qt Test runs the real application objects offscreen
    (`QT_QPA_PLATFORM=offscreen`, `SDL_AUDIO_DRIVER=dummy`) under ctest label
    `gui`, with the menu audit under `gui-audit`.
  - Scenarios: quickstart.md "Automated".
  - Pacing is soak-tested in Release on CI with a fake clock for
    determinism, and on hardware during the manual test.
- **Manual.** Each phase's checklist (quickstart.md "Manual checklists")
  gives build and launch commands and what to try. The PR merges only after
  the maintainer reports that it passed.

## R13. Packaging

**Decision.**
- Windows: `windeployqt` plus a zip.
- macOS: an app bundle via `macdeployqt`, ad-hoc signed.
- Linux: an AppImage via `linuxdeploy` with its Qt plugin, plus a `.desktop`
  file and a MIME association for `.nes`.
- Packages carry the license files.
- Packaging runs on tags and weekly, not on every PR. Signing with developer
  certificates is out of scope for 1.0.

## R14. Movie format

**Decision.** `.cnm`, documented in
[library-layout.md](contracts/library-layout.md):
- A JSON header with the format version, the game identity, the core
  version, the region, the start point (power-on or an embedded state), the
  frame count and re-record count.
- A binary body of two bytes per frame (ports 1 and 2).
- Reset and power events are recorded as flagged frames.

**Why.**
- It is simple and exactly matches what the core consumes (a per-frame
  `ButtonSet`).
- FM2 (FCEUX) import is a possible later feature. Its semantics (subtitles,
  FDS, Zapper commands) exceed the core's scope.

## R15. Accessibility and platform conventions

**Decision.**
- **Standard Qt widgets.** Dialogs use them, so screen readers get names for
  free. Custom views (the library grid, debug views) set
  `QAccessible` names.
- **Platform menus.** `QAction::MenuRole` moves About, Settings and Quit to
  the application menu on macOS.
- **Native dialogs.** The platform's native file dialogs are used.
- **Theme.** The system light or dark theme is followed through Qt's palette.
- **Fullscreen.** It is borderless on the current screen. The menu bar shows
  on Alt or when the pointer is at the top edge (US3).
