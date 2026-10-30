# Feature Specification: NES Emulation Core

**Feature Branch**: `001-emulation-core`

**Created**: 2026-10-17

**Status**: Draft

**Input**: User description: "emulation core"

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Headless Deterministic Game Execution (Priority: P1)

As an emulator frontend developer, I need the core to load a standard NES ROM and
execute it frame by frame, delivering video frames and audio samples as pure data —
with no display, audio device, or windowing dependency — so that any consumer
(Qt GUI, MCP server, or CI test harness) can run games identically.

**Why this priority**: Nothing else in the project exists without basic execution.
This slice alone is a viable MVP: a consumer can load a simple commercial game,
advance it frame by frame, and receive correct audio/video data.

**Independent Test**: Load a known commercial game that uses the simplest mapper,
run 60 frames, and compare the resulting video frames and audio samples against a
golden reference capture; repeat the run and require byte-identical output.

**Acceptance Scenarios**:

1. **Given** a valid, simple-mapper commercial ROM, **When** the core is advanced
   60 frames, **Then** it delivers 60 complete native-resolution video frames and
   the region-correct number of audio samples per frame, and a second identical run
   produces byte-identical output.
2. **Given** an environment with no display and no audio hardware (CI container),
   **When** a game is loaded and run, **Then** execution completes normally and
   all output is delivered as data.
3. **Given** a fresh power-on, **When** the core runs the same ROM with no input
   for N frames, **Then** attract-mode/demo behavior matches a real console boot.

---

### User Story 2 - Accuracy Verified Against Test ROMs (Priority: P2)

As an emulator maintainer, I need the core's hardware behavior validated by the
community-standard test ROM suites, so that accuracy is proven against
hardware-validated references rather than assumed from "looks right" gameplay.

**Why this priority**: The project constitution makes test ROM results the
acceptance gate for all emulation behavior. This story creates the verification
machinery every later change depends on.

**Independent Test**: Run the bundled CPU, PPU, and APU test ROMs headlessly and
confirm each reports a pass result; wire this into the automated suite.

**Acceptance Scenarios**:

1. **Given** a CPU instruction-set test ROM, **When** the core runs it to
   completion, **Then** the ROM's result indicator reports pass (all documented
   and undocumented opcodes and addressing modes behave correctly).
2. **Given** PPU rendering/timing test ROMs, **When** run, **Then** each reports
   pass, including sprite 0 hit, sprite overflow, and mid-frame timing behavior.
3. **Given** APU test ROMs, **When** run, **Then** each reports pass for channel
   behavior, frame counter, and interrupt timing.

---

### User Story 3 - Programmatic Run Control (Priority: P3)

As an automation client (AI agent via MCP, test harness, or TAS tool), I need
precise control over execution: power-on/reset, run for a duration, advance a
single frame, and single-step one CPU instruction, so that I can observe and
react to exact machine states.

**Why this priority**: Automation parity is a constitutional principle; precise
stepping is the foundation for AI-driven play and debugging tooling.

**Independent Test**: Pause a running game, single-step instructions while
reading CPU state, then resume frame-locked execution and confirm the resulting
frame matches the same uninterrupted run.

**Acceptance Scenarios**:

1. **Given** a running game, **When** the client issues a pause, **Then** further
   frame advancement stops until resumed and no machine state changes.
2. **Given** a paused game, **When** the client single-steps one CPU instruction,
   **Then** exactly one instruction executes and the updated CPU state is
   readable.
3. **Given** a game in any state, **When** a reset is issued, **Then** the
   machine reboots exactly as the console's reset button behaves.

---

### User Story 4 - Controller Input Injection (Priority: P4)

As an automation client, I need to set the button state of both standard
controllers at any point (including per-frame), and have games read those states
through authentic controller polling behavior, so that programmatic play is
indistinguishable from physical input.

**Why this priority**: Without input, automation can only watch; with it, AI
agents can actually play games.

**Independent Test**: Inject a button-press sequence into a game with a known
input response (e.g., title screen menu) and confirm the game reacts exactly as
it does to the equivalent physical input.

**Acceptance Scenarios**:

1. **Given** a game displaying a start menu, **When** the injected Start button
   press is delivered through normal per-frame polling, **Then** the game
   responds identically to physical input.
2. **Given** both controllers, **When** independent button states are injected,
   **Then** each game port reads only its own controller's state.

---

### User Story 5 - Save States and Deterministic Restore (Priority: P5)

As a TAS creator or AI-driven verifier, I need to capture the complete machine
state at any moment and restore it later, with post-restore execution
byte-identical to an uninterrupted run, so that exploration and verification can
branch from any point.

**Why this priority**: Deterministic restore is what makes rewind, TAS tooling,
and AI experimentation cheap; it depends on P1-P3 being solid first.

**Independent Test**: Run a game to frame 1,000, save; continue to frame 2,000
recording outputs; restore the frame-1,000 state and rerun — outputs to frame
2,000 must be byte-identical.

**Acceptance Scenarios**:

1. **Given** a running game, **When** a save state is captured mid-frame and
   restored, **Then** subsequent outputs are byte-identical to the original run.
2. **Given** a save state, **When** it is restored repeatedly, **Then** every
   restore produces identical subsequent behavior.
3. **Given** a game with battery-backed saves, **When** the session ends,
   **Then** cartridge save data persists for the next session.

---

### User Story 6 - Broad Commercial Library Support (Priority: P6)

As a player, I need games built on the most common cartridge mappers to run
correctly — covering the large majority of the commercial library — so that the
emulator is useful for real gaming, not just tech demos.

**Why this priority**: Mapper coverage is what turns an accurate CPU/PPU demo
into an emulator people can use; it builds directly on all prior stories.

**Independent Test**: Run a curated reference list of commercial games across the
initial mapper set and verify each reaches gameplay with correct graphics,
audio, and input.

**Acceptance Scenarios**:

1. **Given** a curated list of commercial games spanning the initial mapper set,
   **When** each is run, **Then** each reaches playable gameplay with correct
   rendering, sound, and control response.
2. **Given** a game that uses a mapper outside the initial set, **When** loaded,
   **Then** the core reports the mapper as unsupported with a clear error rather
   than misbehaving silently.

### Edge Cases

- What happens when a ROM file is truncated, corrupt, or not a valid NES image?
  (Rejected with a specific, actionable error identifying the problem.)
- What happens when a ROM uses an unsupported or unimplemented mapper?
  (Rejected with the mapper identity named, never silently mis-executed.)
- What happens when a ROM header declares a region different from the loaded
  timing? (Region is detected and reported; timing follows the header.)
- What happens when a save state from an older core version is loaded?
  (Rejected cleanly until the state format is declared stable — see Assumptions.)
- What happens when input is injected between polls, or read while no input was
  set? (Authentic controller behavior, including unpressed/open states.)
- What happens with ROMs containing a trainer section, unusual mirroring flags,
  or NES 2.0 extended fields? (Parsed per the documented formats without
  data loss or silent misinterpretation.)

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The core MUST load and validate iNES 1.0 and NES 2.0 ROM images,
  reporting the cartridge's mapper, mirroring, region, and battery flag, and MUST
  reject invalid or truncated images with an error that names the specific defect.
- **FR-002**: The core MUST emulate the console CPU's complete documented
  instruction set, addressing modes, and documented unofficial opcodes with
  correct cycle counts, per the nesdev reference documentation.
- **FR-003**: The core MUST emulate the picture processing unit including
  background and sprite rendering (limits, priority, flipping), scrolling,
  mid-frame register effects, sprite 0 hit, sprite overflow, and
  rendering-disabled behavior.
- **FR-004**: The core MUST emulate the audio processing unit: both pulse
  channels, triangle, noise, and sample playback channels, the frame counter,
  and interrupt generation, with mixer output levels per the reference
  documentation.
- **FR-005**: The core MUST implement the console memory map including RAM
  mirroring, register address space, and direct memory access transfers, with
  correct read/write side effects.
- **FR-006**: The core MUST support the initial mapper set — NROM (0), MMC1 (1),
  UxROM (2), CNROM (3), and MMC3 (4) — including bank switching, mirroring
  control, and battery-backed save RAM where the cartridge declares it. The
  implemented set has since grown (MMC5, AxROM, BNROM, Color Dreams, VRC2a,
  Action 53, MMC1A); the list is in the README and `core/src/cutenes/mappers/nrom.cpp`.
- **FR-007**: The core MUST accept button states for two standard controllers
  and present them to software through authentic serial controller polling
  behavior.
- **FR-008**: The core MUST expose run control — power-on, reset, run for a
  number of frames, advance exactly one frame, and single-step one CPU
  instruction — with pause and resume.
- **FR-009**: The core MUST deliver each completed video frame as native
  resolution pixel data (256x240 for NTSC timing) to the consumer when the frame
  ends.
- **FR-010**: The core MUST deliver audio as a continuous stream of mixed,
  region-correct sample data to the consumer.
- **FR-011**: The core MUST capture and restore complete machine state —
  CPU, PPU, APU, memory, mapper, and input — at any point, including mid-frame.
- **FR-012**: The core MUST produce byte-identical video and audio output for
  identical ROM, input sequence, and starting state across repeated runs.
- **FR-013**: The core MUST operate with no dependency on display, audio, or
  windowing facilities of the host system.
- **FR-014**: The core MUST support both NTSC and PAL timing and report which
  applies for a loaded cartridge.
- **FR-015**: The core MUST pass the project's bundled hardware-validated test
  ROM suite (CPU, PPU, APU) with all targeted ROMs reporting pass.
- **FR-016**: The core MUST provide consumers read access to machine state
  (CPU registers and memory contents) sufficient to observe execution without
  altering it.

### Key Entities

- **Cartridge**: A loaded ROM image's identity — format version, mapper,
  program/character data, mirroring mode, region, battery flag. Source of the
  game being emulated.
- **Machine State**: The complete observable condition of the emulated console
  (CPU, PPU, APU, memory, mapper banks, input) at an instant; the substance of
  a save state.
- **Video Frame**: One complete native-resolution image of emulated output,
  delivered per frame boundary.
- **Audio Buffer**: A bounded stream of mixed audio samples corresponding to
  emulated time.
- **Controller State**: The button set (directions, A/B, Start, Select) for each
  of the two gamepad ports at a point in emulated time.
- **Run Command**: A consumer-issued instruction to power on, reset, run,
  pause, frame-advance, or single-step the machine.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: All targeted test ROMs in the project's acceptance suite report
  pass (100% of the suite).
- **SC-002**: Identical runs (same ROM, inputs, starting state) produce
  byte-identical output in 100% of repeated trials, including runs that cross a
  save/restore boundary.
- **SC-003**: The core sustains real-time emulation — 60.0988 frames per second
  (NTSC) or 50.0070 (PAL) — on commodity hardware, with at least 2x headroom
  (fast-forward capable).
- **SC-004**: Every game on a curated reference list spanning the initial mapper
  set reaches playable gameplay with correct graphics, audio, and input.
- **SC-005**: The full test suite runs end-to-end in a display-less, audio-less
  continuous integration environment.
- **SC-006**: Save/restore round trips complete with no observable divergence
  from uninterrupted execution in 100% of tested games.

## Assumptions

- Accuracy is defined by the nesdev.org reference documentation and the chosen
  hardware-validated test ROM suites (e.g., the community-standard CPU/PPU/APU
  test sets); the suite's targeted ROM list is finalized during planning.
- Both NTSC and PAL regions are in scope for the core; Famicom Disk System,
  unlicensed/prototype mappers, and exotic later mappers beyond the implemented
  set are out of scope for this feature and may become future features (VRC4
  and NINA-001 are refused by design).
- NSF/NSFe music playback is out of scope; this feature emulates game cartridges.
- The core produces data; presentation (screen rendering, audio playback,
  shaders) is the consumer's responsibility per the constitution's core/UI
  separation principle.
- The save-state format is unstable until the core's first stable release; state
  files from other versions may be rejected.
- No ROMs or copyrighted game data are distributed with the project; test
  references are maintainer-provided.
- Physical accessories beyond the two standard controllers (Zapper, multiplayer
  adapters, keyboards) are out of scope for this feature.
