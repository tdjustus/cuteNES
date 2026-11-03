# Design philosophy

The principles behind cuteNES, why each one exists, and what it has bought
the project so far. They are binding: the
[constitution](../.specify/memory/constitution.md) states them as rules,
and this document explains them.

Each section separates the **general idea**, which applies to any emulator
or any program that models real hardware, from **how cuteNES applies it to
the NES**.

## 1. The hardware is the specification

**General.** An emulator has a specification already: the machine it
emulates. The job is to match that machine, not to make something that
"looks right". So every behavior has a source: a hardware document, a
measurement, or a test program written by someone who checked the real
thing. A value with no source is a guess, and a guess is recorded as a
known gap, not hidden.

**For the NES.** The normative reference is the
[NESdev wiki](https://www.nesdev.org/wiki/NES_reference_guide). Code
comments cite the wiki page or the test ROM behind a decision. Timing
constants live in one header (`core/src/cutenes/system/timing.hpp`), with
their sources. Known inaccuracies are GitHub issues in the 1.0 milestone.

**What it bought.** Every accuracy fix has a reference to argue from. When
the last failing timing test (#42) was fixed, the model was rewritten from
the wiki's description of how the PPU status register is read. Tuned
constants that had made other tests pass were removed, and the real cause
turned up: the CPU checked for an NMI at different points in different
kinds of cycle.

## 2. Tests come from outside the emulator

**General.** A test that records what a program does today and checks it
does the same tomorrow proves only that nothing changed. It cannot say
whether the behavior was ever right. A test's expected value must come from
outside the program: a specification, or a reference program's own verdict.
For behavior changes, write the test first and see it fail, so you know it
can fail.

**For the NES.** The community's test ROMs (blargg's CPU, PPU and APU sets,
nestest, the MMC3 IRQ tests, Holy Mapperel and others) report their own
result, on screen or at `$6000`. They are the acceptance gate: a regression
in them blocks a change, whatever its value. Unit tests take their expected
values from the wiki.

Where the only practical check is a picture, cuteNES pins a frame hash. A
pinned hash is weaker, and the project says so: each one carries a status
(verified against a reference, checked by eye, or unverified), and a moved
hash must be explained before it is re-pinned. See
[`test-corpus.md`](test-corpus.md).

**What it bought.** Changes to the deepest parts of the timing model can be
made with confidence. When a fix moves a pinned hash, the question is "why
did this move, and is the new one right?", answered by comparing against
the previous build, not "update the hash".

## 3. A headless, deterministic core

**General.** Keep the model of the machine apart from everything that shows
it or controls it: no windowing, sound, network or protocol code inside it.
Make it deterministic: the same starting state and the same inputs give
the same output, byte for byte, on every platform and every run. Then the
core can be tested without a display, run faster than real time, rewound,
replayed, and driven by programs.

**For the NES.** `cutenes-core` is C++20 with no runtime dependencies. It
takes a ROM and controller input and produces 256x240 frames and audio
samples. Time is a single master clock counted in integer ticks, so nothing
depends on the host's clock or floating-point rounding. Save states round
trip byte for byte.

**What it bought.** Rewind, movies, save states and the AI interface are
all built on determinism rather than each needing its own machinery. CI
runs nearly two hundred ROM tests on three operating systems with no
display.
Determinism is itself tested: a ROM run twice gives identical output.

## 4. Every consumer uses the same public API

**General.** If the UI, the tests and the automation layer all use the
same public interface, with no privileged back doors, then each one
exercises the others' path. A feature that works in one works in all, and
a test of the API is a test of the product.

**For the NES.** The Qt application, `cutenes-server` and the test suite
link only the core's public headers. The layers are linked in one
direction, `core` ← `api` ← `qt` ← `app`, and the build enforces it. See
[`architecture.md`](architecture.md).

**What it bought.** The desktop application was built on a core and a
command layer already tested through the headless server, so its bugs were
in the new layer, not the old.

## 5. Automation parity

**General.** If a person can do something, a program can do it the same
way. Each user action is a named command with a typed schema, and the
menus call those commands. Then automation is never a second-class copy of
the UI that falls behind it.

**For the NES.** Every menu item is a `gui.*` command; the core has a
`core.*` command per public operation. They are served over MCP (for AI
agents) and JSON-RPC (for scripts) from one registry. A test (`gui-audit`)
checks that the menu actions, the contract's table of them and the `gui.*`
catalog agree, and that every action is wired. See [`automation.md`](automation.md).

**What it bought.** AI agents can play, test and debug games, which is
how much of cuteNES's own GUI testing is done: a change is checked by
driving the real application through its commands.

## 6. Cross-platform by construction

**General.** Support every target platform from the first commit, from one
codebase, with platform code behind interfaces. Retrofitting a second
platform late costs far more than building for three from the start.

**For the NES.** Windows, macOS and Linux build from one tree with CMake.
Qt 6 is the only UI toolkit, and SDL3 handles sound and gamepads. CI builds
and tests all three on every pull request.

## 7. Write it down first

**General.** Decide what a feature is before building it: a specification
of what it does and why, a plan of how, and a task list. Record each
decision with its alternatives. The written record is what lets someone
else, or you later, understand why the code is the way it is.

**For the NES.** Every feature goes through Spec Kit (specify, clarify,
plan, tasks, implement), and the documents live in [`specs/`](../specs/).
Source control is one branch at a time, each merged before the next begins
([`source-control.md`](source-control.md)).
