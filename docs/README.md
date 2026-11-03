# cuteNES documentation

The [README](../README.md) is for players. These documents are for
everyone else: people who build cuteNES from source, drive it from a
program, embed its core, or work on it.

## How cuteNES is designed

- [Design philosophy](design-philosophy.md): the principles behind the
  project, why each exists, and what it bought, separating the general idea
  from how it applies to the NES.
- [Architecture](architecture.md): the layers, the one emulation thread,
  the other threads, and how frames, sound, input and commands move.
- [Core internals](core-internals.md): the master clock, catch-up, the CPU,
  PPU and APU, mappers, save states and determinism.

## Using cuteNES

- [User guide](user-guide.md): the desktop application, menu by menu. It is
  also in the application, under Help > User Guide.
- [Automation: MCP and the API](automation.md): `cutenes-server`, MCP for AI
  agents, the JSON-RPC API for scripts, `--attach`, and the security
  defaults.
- [Using the core](using-the-core.md): the emulation core as a C++ library
  in your own program, and what it supports.

## Working on cuteNES

- [Development process](development-process.md): from an issue to a
  release: specs, branches, tests first, checking in the real program,
  pull requests and releases.
- [Contributing](contributing.md): step-by-step recipes for adding a
  mapper, an automation command or a GUI feature.
- [Building and testing](building-and-testing.md): building from source,
  the test groups and what each proves, CI, and the ROM harness.
- [Test corpus](test-corpus.md): the test ROMs, how each folder is used, and
  how a frame hash is pinned.
- [Mappers](mappers.md): the supported cartridge boards and their
  revision-dependent behavior.
- [Packaging](packaging.md): how the macOS, Windows and Linux packages are
  made.
- [Source control](source-control.md): branches, pull requests, CI policy,
  versions, tags and publishing a release.

## Specifications

The project is specification-driven: each feature has a spec, a plan and a
task list under [`specs/`](../specs/), written before its code. The
principles every feature follows are in the constitution,
[`.specify/memory/constitution.md`](../.specify/memory/constitution.md).

- [001: emulation core](../specs/001-emulation-core/spec.md), its API
  contract and the timing analyses in
  [`analysis/`](../specs/001-emulation-core/analysis/);
- [002: AI and MCP server](../specs/002-ai-mcp-server/spec.md);
- [003: Qt frontend](../specs/003-qt-frontend/spec.md).

## Still to come

Issue [#116](https://github.com/tdjustus/cuteNES/issues/116) adds, before
1.0: decision records and a glossary.
