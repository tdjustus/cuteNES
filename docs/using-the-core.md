# Using the core in your own program

The emulation core (`core/`) is a C++20 library with no runtime
dependencies and no UI. It takes a ROM and controller input and produces
video frames and audio samples, deterministically: the same ROM and inputs
give byte-identical output on every platform. The desktop application and
`cutenes-server` are both built on its public API, and nothing else.

## Adding it to a CMake project

```cmake
include(FetchContent)
FetchContent_Declare(cutenes-core
    GIT_REPOSITORY https://github.com/tdjustus/cuteNES.git
    GIT_TAG core-v0.1.0)
FetchContent_MakeAvailable(cutenes-core)
target_link_libraries(your_app PRIVATE cutenes-core::cutenes-core)
```

Core releases are tagged `core-v<version>`, separately from the
application's releases ([`source-control.md`](source-control.md),
"Versions and tags").

## The API

The public API contract is
[`specs/001-emulation-core/contracts/core-api.md`](../specs/001-emulation-core/contracts/core-api.md),
and the header is `core/include/cutenes/cutenes.hpp`.

For debugging, `Machine::dumpMemory("debug")` writes a `<datetime>.dmp` with
the CPU bus, PPU registers, OAM, palette, PRG-RAM and the whole PPU bus
(pattern tables, nametables, palette). `Machine::memoryDump()` returns the
same bytes without touching the filesystem. The layout is in the core
contract.

## What it supports

| | Supported |
|---|---|
| CPU/PPU/APU | Cycle-stepped 6502 including unofficial opcodes, dot-stepped PPU, APU with DMC DMA |
| Regions | NTSC and PAL (PAL is validated per frame and for the APU, not per PPU dot) |
| Mappers | 0 NROM, 1/155 MMC1 (incl. SUROM/SXROM), 2 UxROM, 3 CNROM, 4 MMC3, 5 MMC5 (no expansion audio), 7 AxROM, 11 Color Dreams, 22 VRC2a, 28 Action 53, 34 BNROM |
| Headers | iNES 1.0 and NES 2.0 (RAM sizes, TV system, submappers) |
| Save states | Byte-identical round trip |

Revision-dependent mapper behavior (MMC1A/B, MMC3 A/B) is in
[`mappers.md`](mappers.md). The normative hardware reference is the
[NESdev NES Reference Guide](https://www.nesdev.org/wiki/NES_reference_guide).
The Qt embedding design is
[`ui-embeddable-design.md`](../specs/001-emulation-core/ui-embeddable-design.md).
