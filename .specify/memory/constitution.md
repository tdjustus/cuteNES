# cuteNES Constitution

## Core Principles

### I. Hardware Fidelity First

The nesdev.org NES Reference Guide (https://www.nesdev.org/wiki/NES_reference_guide) and its
sub-pages are the normative specification for all emulated hardware behavior.

- Every emulated component (6502/2A03 CPU, 2C02 PPU, APU, cartridge mappers, memory mapping)
  MUST implement the documented behavior of the corresponding hardware.
- Undocumented behaviors (illegal opcodes, stray read/write side effects, PPU sprite
  glitches, open-bus values) MUST follow nesdev research where needed for compatibility,
  and each decision MUST cite its source document or test ROM in code comments or docs.
- No magic constants or guessed timing values without a traceable hardware reference.
- Known inaccuracies MUST be recorded as issues; they MUST NOT be silently accepted.

Rationale: an emulator's quality is defined by accuracy, and every other feature
(rewind, netplay-adjacent replay, AI control via MCP) compounds accuracy errors.

### II. Core/UI Separation

The emulation core (CPU, PPU, APU, cartridge/mappers, input state machine, save-state
serialization) MUST be built as a standalone library with zero dependencies on Qt,
GUI code, or MCP protocol code.

- The core MUST be independently buildable and fully testable headlessly, including in
  CI environments with no display or audio device.
- The Qt frontend, the MCP server, and the test suite are equal consumers of the core's
  public API; none of them MAY bypass it with privileged access.
- Breaking changes to the core's public API require a version bump and a migration note.

Rationale: the MCP server requires headless, scriptable operation and the test suite
requires GUI-free verification; a shared core is the only architecture that delivers
both without duplication.

### III. Test ROMs Are the Acceptance Gate (NON-NEGOTIABLE)

All changes to emulation behavior MUST pass the project's automated test ROM suite
before merge.

- The suite MUST include community-standard hardware-validated test ROMs (e.g., nestest,
  blargg's CPU/PPU/APU test sets, and successors enumerated in the feature specs) plus
  project-written unit tests for each core component.
- Tests are written first for core behavior changes: tests → review/approval → observed
  failure → implementation. Red-Green-Refactor is strictly enforced for the core.
- A regression in test ROM results blocks the change, whatever the feature value.

Rationale: emulator correctness is only verifiable against known-good,
hardware-validated reference outputs; internal consistency proves nothing.

### IV. Cross-Platform by Construction

cuteNES MUST build and run on Windows, macOS, and Linux from a single codebase.

- Qt 6 or later is the only sanctioned UI toolkit; the codebase MUST target the C++20
  standard or later, compiled with a conforming native toolchain on each platform.
- Platform-specific code (filesystem paths, gamepad handling, audio/backend glue) MUST
  be isolated behind interfaces; it MAY NOT leak into the emulation core or UI logic.
- CI MUST build and run the test suite on all three supported platforms for every
  release.

Rationale: as an open-source project, reach across the three major desktop platforms
is a founding requirement; Qt was chosen specifically to make this cheap to maintain.

### V. Automation Parity

Every user-facing capability MUST also be available programmatically through the MCP
server: loading ROMs, controller input injection, frame stepping, pause/resume,
save/load states, memory and register read/write, and screen capture.

- No feature MAY exist as GUI-only: if it is in the frontend, it is exposed to MCP
  clients through the same core API.
- The MCP interface is a first-class, versioned, schema-documented API — not an
  afterthought or a debug hook.
- Determinism: given an identical ROM, input sequence, and starting state, emulation
  MUST produce identical results, enabling AI-driven verification and TAS-style replay.

Rationale: AI drivability is this project's differentiating feature; parity and
determinism keep the GUI and automation surfaces from diverging.

## Emulation & Platform Constraints

- Normative reference: the nesdev.org NES Reference Guide and its sub-pages.
- Feature scope: parity with established emulators — save states, rewind,
  fast-forward, input mapping, cheat search, movie/TAS recording, debug views
  (disassembly, CPU/PPU/pattern/nametable/memory viewers), video filters, and audio
  configuration. Features are enumerated and specified per feature with Spec Kit;
  scope creep beyond ratified specs is not permitted without a spec amendment.
- ROM format support: iNES 1.0 and NES 2.0 (iNES 2.0) header parsing is required;
  additional formats are optional and must be specified before implementation.
- Performance: real-time emulation at native frame rates (60.0988 fps NTSC,
  50.0070 fps PAL) on commodity hardware, with headroom for fast-forward multiples.
- Audio: APU emulation with configurable resampling, latency, and sync strategy.
- Licensing: MIT (Expat). All project-authored code MUST be distributed under the
  MIT license. Qt MUST be consumed under LGPL-3.0 via dynamic linking, with LGPL
  notices and relinking provisions satisfied in official releases.
- Third-party dependencies MUST be minimized and MUST be open source and
  license-compatible with the MIT-licensed project (permissive licenses, or
  copyleft libraries consumed via dynamic linking such as Qt).

## Development Workflow & Quality Gates

- Spec Kit workflow is the path for every feature: constitution → specify → clarify →
  plan → tasks → implement. Features begin with `/speckit-specify`, never with code.
- Every change MUST pass: the automated test suite (Principle III), CI builds on all
  three platforms (Principle IV), and review by at least one maintainer for core
  changes.
- Source control, CI and release management follow `docs/source-control.md`:
  one branch at a time, each merged before the next begins; known bugs and
  roadmapped features are tracked in the GitHub milestone for the next
  release; version 1.0 is released only when its milestone has no open issues.
- Hot paths (CPU/PPU/APU stepping) MAY be optimized only after correctness is locked
  by tests, and optimizations MUST NOT regress real-time performance as measured by
  the project's benchmarks.
- New core components MUST be documented in code, with hardware citations per
  Principle I.
- Release gates: full test ROM suite passes, MCP schema validates, all
  cross-platform builds are green, and the release milestone has no open issues.

## Governance

- This constitution supersedes all other project practices; conflicts resolve in its
  favor.
- Amendments require a documented proposal, maintainer approval, a version bump
  following semantic versioning (MAJOR: principle removal or redefinition; MINOR: new
  principle or materially expanded guidance; PATCH: clarification or wording), and a
  migration note for affected work.
- All reviews MUST verify constitution compliance, with explicit attention to
  Principles II, IV, and V (core separation, cross-platform, automation parity).
- Feature scope decisions are governed by the specs that reference this constitution;
  expanding scope requires amending the relevant spec, not silent addition.

**Version**: 1.1.0 | **Ratified**: 2026-10-17 | **Last Amended**: 2026-10-27
