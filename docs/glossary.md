# Glossary

Terms used across cuteNES's code and documents. NES hardware terms follow
the [NESdev wiki](https://www.nesdev.org/wiki/NES_reference_guide), where
each has a fuller page.

## NES hardware

| Term | Meaning |
|---|---|
| **2A03 / 2A07** | The NES's CPU chip (NTSC / PAL): a 6502 core without decimal mode, plus the APU and DMA. |
| **2C02 / 2C07** | The PPU chip (NTSC / PAL). |
| **6502** | The 8-bit processor family the 2A03 is built on. |
| **A12** | Bit 12 of the PPU address bus. MMC3-class mappers count its rising edges to time scanline IRQs. |
| **APU** | Audio processing unit: two pulse channels, a triangle, noise and the DMC. |
| **Bank switching** | A mapper swapping which part of a large ROM appears in a fixed window of the address space. |
| **Battery save, SRAM** | PRG-RAM kept powered by a battery on the cartridge, so a game's save survives power-off. cuteNES writes it to a `.sav` file. |
| **CHR ROM / CHR RAM** | The cartridge's pattern (tile) memory, read by the PPU: fixed ROM, or RAM the game fills. |
| **CIRAM** | The console's 2 KiB of nametable RAM. |
| **DMA** | Direct memory access. Sprite (OAM) DMA copies 256 bytes into OAM; DMC DMA fetches sample bytes. Both stall the CPU. |
| **DMC** | Delta modulation channel: the APU channel that plays samples from memory. |
| **Dot** | One PPU clock; one pixel position. A scanline has 341. |
| **Frame sequencer, frame counter** | The APU's timer that clocks envelopes, sweeps and length counters, and can raise an IRQ. |
| **iNES, NES 2.0** | The ROM file header formats: sizes, mapper number, mirroring, and in NES 2.0 RAM sizes, TV system and submapper. |
| **IRQ** | Maskable interrupt, raised by the APU or a mapper. |
| **Mapper, board** | The cartridge's hardware beyond the ROM chips: bank switching, mirroring, IRQs. Identified by the header's mapper number. |
| **Master clock** | The console's main oscillator. Every other clock divides it. |
| **Mirroring** | How the PPU's four nametable slots map onto CIRAM's two: horizontal, vertical, single-screen, or mapper-controlled. |
| **Nametable** | A 32x30 grid of tile numbers that makes up the background. |
| **NMI** | Non-maskable interrupt. The PPU raises it at the start of vertical blank when enabled. |
| **NTSC, PAL, Dendy** | Television standards and their console variants, with different clocks and frame rates. cuteNES supports NTSC and PAL. |
| **OAM** | Object attribute memory: the PPU's 256-byte sprite table. |
| **Odd-frame skip** | On NTSC, with rendering on, every other frame is one dot shorter. |
| **Open bus** | Reading an address nothing drives returns the last value left on the bus. |
| **Pattern table** | The 8x8 tiles in CHR memory that the background and sprites are drawn from. |
| **PPU** | Picture processing unit: draws the background and sprites, one dot at a time. |
| **PRG ROM / PRG-RAM** | The cartridge's program memory (ROM) and its work or save RAM. |
| **Scanline** | One row of the frame: 262 per frame on NTSC, 312 on PAL, 240 of them visible. |
| **Sprite-0 hit** | A PPU flag set when sprite 0 overlaps the background; games use it to split the screen. |
| **Submapper** | NES 2.0's refinement of a mapper number, telling board revisions apart. |
| **VBL, vertical blank** | The scanlines after the visible picture, when games update video memory. Its flag is bit 7 of `$2002`. |
| **`$2002`** | PPU status register: VBL, sprite-0 hit and sprite overflow. Reading it clears VBL. |

## Emulation

| Term | Meaning |
|---|---|
| **Catch-up** | Running a component forward to the current time just before an access that could see it, instead of in lockstep. |
| **Cycle-accurate, dot-accurate** | Timing every bus access to the right CPU cycle, and every PPU effect to the right dot. |
| **Determinism** | The same ROM, inputs and starting state give byte-identical output, every run, on every platform. |
| **Dynamic rate control** | Nudging the audio playback rate slightly to keep the sound queue at its target, absorbing the difference between the emulator's clock and the sound card's. |
| **Emulation revision** | A counter in the core (`cutenes::emulationRevision`) that rises whenever the same input can give different output. Movies record it and warn on playback when it differs. It is separate from the core version, which also rises for new features (#166). |
| **Golden hash, pinned hash** | A recorded hash of a frame (or audio) a test compares against. It records what the emulator drew, so it carries a verification status. |
| **Movie** | A recording of the input for every frame, replayed from a start point. cuteNES's format is `.cnm`. |
| **Rewind** | Stepping a game backward through states captured every frame. |
| **Save state** | A snapshot of the whole machine, from which it resumes exactly. |
| **Test ROM** | A program written to check hardware behavior, which reports its own pass or fail. |

## The project

| Term | Meaning |
|---|---|
| **`--attach`** | `cutenes-server --attach`: relays a stdio MCP client to the running desktop application. |
| **Automation parity** | The rule that every user-facing action is also a command a program can call. |
| **Blob** | A server-side buffer for data too large for one call (ROM uploads, state files), referred to by a handle. |
| **Command, catalog** | A named operation with JSON Schemas (`core.*`, `session.*`, `api.*`, `gui.*`). The catalog is the list of all of them (`api.catalog`). |
| **Constitution** | `.specify/memory/constitution.md`: the project's binding principles. |
| **`ci-ok`** | The single required CI check: it passes only when every build and test job succeeded or was skipped. |
| **Corpus** | The set of test ROMs and scripted runs in `tests/`. |
| **Emulation thread** | The one thread that touches the `Machine`; every command runs on it. |
| **FR, SC, US** | A spec's functional requirement, success criterion and user story, numbered (FR-070, SC-003, US5). |
| **G0xx, T0xx, A0xx** | Task numbers in feature 003, 001 and 002's `tasks.md`. |
| **Host** | The program a `Session` runs in (`IHost`): headless, or the GUI, which registers the `gui.*` commands. |
| **Known-failing** | A ctest label for tests of documented discrepancies; none at present. |
| **Library** | The folder of games and everything cuteNES writes for them (`Documents/cuteNES` by default). |
| **`Machine`** | The core's public object: one emulated NES. |
| **MCP** | Model Context Protocol: how AI agents discover and call tools. |
| **Milestone** | The GitHub milestone holding every known bug and planned feature for the next release. |
| **`.nesx`, nesx mode** | The planned NES Expansion format for 3.0 (#102): a package of scripts, patches and assets applied over a ROM the user owns, run in nesx mode on its own core (#143). It comes with the opt-in nesx plugin (#198). |
| **Plugin** | An optional feature as a dynamic library that cuteNES downloads when the user turns it on in Settings, locked to the app's version (#201, from 2.0). Video export and nesx are planned as plugins. |
| **Real-time play** | The session running frames on the host's clock, for the desktop application. |
| **`rom_harness`** | The command-line tool that runs ROMs for tests: verdicts, frames, determinism, save states, speed. |
| **`roms-local/`** | The gitignored folder for a developer's own commercial ROMs. |
| **`Session`** | The `cutenes-api` object that owns the `Machine` and its command queue. |
| **Spec Kit** | The specification workflow (specify, clarify, plan, tasks, implement) used for every feature. |
| **Surface version** | The `MAJOR.MINOR` version of the command catalog (`api.version`), separate from the application's and the core's. |
| **Tier 1, tier 2** | Test ROM groups, both run on every PR: tier 1 is the gate set, each reporting its own verdict; tier 2 is broader, some of it checked by frame hash. |
