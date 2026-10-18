# cuteNES

An open-source, cross-platform NES emulator built on a hardware-accurate,
headless, deterministic emulation core — with a first-class MCP server so
AI agents can connect to and drive the emulator.

- **Core**: native C++20, no runtime dependencies, byte-identical
  cross-platform output, validated against the community test-ROM suites.
- **Frontends** (planned): Qt 6 desktop app; MCP server for AI automation.
- **Normative hardware reference**: the [NESdev NES Reference Guide]
  (https://www.nesdev.org/wiki/NES_reference_guide).

## Building the core

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Requires CMake >= 3.21 and a C++20 compiler (MSVC 2022+, AppleClang 15+,
or GCC 12+). Tests fetch GoogleTest at configure time (standalone builds
only) and need `git clone --recurse-submodules` for the test-ROM archive.

## Consuming the core

```cmake
# From the Qt app / MCP server:
FetchContent_Declare(cutenes-core GIT_REPOSITORY ... GIT_TAG v0.1.0)
FetchContent_MakeAvailable(cutenes-core)
target_link_libraries(your_app PRIVATE cutenes-core::cutenes-core)
```

Public API contract: `specs/001-emulation-core/contracts/core-api.md`.

## Project governance

- Constitution: `.specify/memory/constitution.md`
- Feature specs & plans: `specs/`
- Licensing: MIT for project code (see LICENSE); test-ROM provenance in
  `third_party/NOTICE.md`.

## Status

Feature 001 (emulation core) is in implementation — see
`specs/001-emulation-core/tasks.md`.
