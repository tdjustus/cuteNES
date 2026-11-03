# Decision records

The significant decisions behind cuteNES: what was chosen, what was
rejected, and why. Each entry is a summary. The full record, with its
sources, is the linked section of a feature's `research.md`, written when
the decision was made. The end of this document collects lessons learned
along the way.

Feature 001 is the core
([`research.md`](../specs/001-emulation-core/research.md), D1 to D12),
002 the API and MCP server
([`research.md`](../specs/002-ai-mcp-server/research.md), R1 to R11), and
003 the desktop application
([`research.md`](../specs/003-qt-frontend/research.md), R1 to R16).

## The core

| Decision | Chosen | Rejected, and why | Record |
|---|---|---|---|
| How components advance | An instruction-level CPU with exact per-access cycles; the PPU and APU caught up at every bus access; one thread | Threads per component (as higan and ares): no accuracy gain on the test suites, a risk to determinism. A scanline PPU: cannot pass the dot-timing tests | 001 D1 |
| Time | One integer master clock; region rates as integer ratios | Floating-point rates: not exact, and not identical across compilers | 001 D2 |
| Determinism | Integer-only emulation and audio, backed by strict floating-point compiler flags | A `double` mixer (as Mesen): identical per build, but not across compilers, and cuteNES promises byte-identical audio on every platform | 001 D7 |
| Audio | The core mixes per APU cycle and resamples to the consumer's rate itself | Resampling in each consumer: the GUI and MCP would hear different sound | 001 D8 |
| Save states | A versioned blob of tagged sections with all sub-cycle state; any version mismatch refused | A positional, unversioned serializer: silent misalignment is the one unacceptable failure | 001 D9 |
| Acceptance | Two tiers of freely licensed community test ROMs: tier 1 reports its own verdict and gates CI; tier 2 is checked by frame or by hand. Commercial games only from a developer's own copies | The constitution's reason for test ROMs over self-consistency: "internal consistency proves nothing" (principle III) | 001 D11 |

## The API and MCP server

| Decision | Chosen | Rejected, and why | Record |
|---|---|---|---|
| Command surface | One command registry with JSON Schemas, served as both JSON-RPC methods and MCP tools | Separate MCP tools calling the API: two catalogs that would drift apart | 002 R1 |
| MCP implementation | Written in C++ inside the project | A Python or TypeScript SDK sidecar: another runtime to install, a second process in the GUI. Rust bindings: a second toolchain | 002 R2 |
| MCP revision | 2026-07-28 (stateless), and 2025-11-25 clients still accepted | Only the newest: mainstream clients upgrade gradually | 002 R3 |
| Concurrency | A session with one emulation thread draining a command queue; transports only enqueue and wait | A lock around the machine: callers would interleave between frames, and the GUI and automation would race | 002 R5 |
| Security | No listener unless asked; loopback only; a bearer token per listener in an owner-only file; Host and Origin checks | OAuth: MCP's authorization spec targets remote servers with an authorization server, and a local single-user tool only needs to keep other local accounts out | 002 R11 |

## The desktop application

| Decision | Chosen | Rejected, and why | Record |
|---|---|---|---|
| Toolkit | Qt 6.8 LTS or later, Widgets, linked dynamically under the LGPL | Qt 6.5: lacks `QRhiWidget`. QML for the main UI: menus, docks and dialogs are Widgets' strength | 003 R1 |
| Rendering | `QRhiWidget` (Metal, Direct3D, Vulkan, OpenGL), with a `QPainter` fallback | | 003 R2 |
| Sound and pacing | SDL3 audio with dynamic rate control (at most ±0.5%); sync to display when its rate is within 1% of the console's, otherwise sync to audio | | 003 R4 |
| Rewind | A state every frame, stored as XOR deltas with run-length encoding in a 64 MiB ring | Snapshots every N frames with re-emulation: more complex audio, slower to rewind | 003 R6 |
| Game identity | The SHA-1 of the ROM data without its header, as No-Intro uses, so re-headered dumps share saves; file names carry the first 8 hex digits, so they stay readable and identically named ROMs stay apart | | 003 R7 |
| GUI commands | Every menu item is a `gui.*` command; handlers run on the emulation thread and post widget work to the GUI thread, so a click and an MCP call run the same code | | 003 R11 |
| Movies | `.cnm`: a JSON header and two bytes of input per frame | FM2 (FCEUX) as the native format: its features exceed the core's scope; import may come later | 003 R14 |
| Self-update | Opt-in; one HTTPS request to the GitHub release list per launch; the download checked against the release's `SHA256SUMS`; the old version kept until the new one is in place | `/releases/latest` only: the newest release may lack this platform's build | 003 R16 |

## Project decisions

These are recorded in [`source-control.md`](source-control.md) rather than
a research file.

- **One branch at a time.** Each pull request is merged before the next
  branch starts. Stacked branches would test changes against a `main` they
  will not land on.
- **No CI run after a merge.** A pull request is tested up to date with
  `main`, so a post-merge run would repeat a tested tree. A weekly run
  catches runner-image drift; the tag run gates releases.
- **Separate application and core versions.** The core's version is written
  into every save state, and until 1.0 a state loads only into its exact
  core version. Raising it with every application release would break
  every user's save states.
- **Pre-1.0 releases are normal GitHub releases marked Latest**, not
  prereleases: every pre-1.0 build is a test build, and GitHub never shows
  a prerelease as Latest.

## Lessons learned

What went wrong or took longer than it should have, and what the project
does differently now.

- **Tuned constants hide a wrong model.** The last failing timing test
  (#42) resisted fixes for a long time, because earlier work had added
  windows and offsets that made other tests pass. The fix came from
  rebuilding the `$2002` read from the wiki's description and removing the
  tuned constants. The cause turned out to be that internal CPU cycles and
  bus cycles sampled NMI at different points. It was found by logging the
  NMI edges under two configurations that should have been equivalent, and
  diffing the logs. Every attempt, including the failed ones, is recorded
  in [`FIX_ATTEMPTS.md`](../specs/001-emulation-core/analysis/FIX_ATTEMPTS.md).
  *Lesson:* when a fix needs a new magic number, suspect the model.
- **NTSC numbers leak into PAL.** Sprite DMA used 12 master ticks per CPU
  cycle, NTSC's figure, on PAL too (#160), and so did the PPU's power-on
  lag (#171). No test caught either, because PAL was checked per frame,
  not per cycle. *Lesson:* a per-cycle quantity comes from
  the region profile, never from a literal.
- **Checks that list what to reject miss new cases.** `ci-ok` failed on
  `failure` and `cancelled` only, so a job whose runner was lost
  (`abandoned`) let a pull request through untested on macOS (#167).
  *Lesson:* list what is accepted, and fail on everything else.
- **What pull requests don't build can still break.** The macOS package
  for alpha 3 failed on the tag, because package jobs run only on tags,
  weekly and on demand. *Lesson:* when build or packaging files changed
  since the last release, run the package jobs on `main` before tagging.
- **A default time is not "never".** The battery saver's last-write time
  started as a default `steady_clock` time point, which is roughly the
  machine's boot time, so on a freshly booted CI runner the first write was
  throttled (#163). *Lesson:* "not yet" is an empty optional, not a zero.
- **Conventions not written down get guessed wrong.** Alpha 2 was first
  numbered "alpha 1" (counted within its version) and marked as a
  prerelease. The counting rule and the Latest rule are now in
  `source-control.md`. *Lesson:* write the convention down the first time
  it is used.
- **Timing changes can break recordings silently.** The #42 and #160 fixes
  kept the core version, so save states still load, but a movie recorded
  before them can replay differently with no warning. The policy is still
  open (#166).
