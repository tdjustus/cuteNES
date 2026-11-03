# Contributing

How to make the most common kinds of change: a mapper, an automation
command, or a GUI feature. Each recipe lists the files to touch, the tests
to write first, and the documents to update. The process around a change
(specs, branches, pull requests) is in
[`development-process.md`](development-process.md); building and running
the tests is in [`building-and-testing.md`](building-and-testing.md).

Three rules apply to every recipe:

- **Tests first** for any change to emulation behavior: write the test, see
  it fail, then make it pass.
- **Expectations come from outside the emulator**: the NESdev wiki, a test
  ROM's own verdict, or a written contract. Never from what the emulator
  outputs today.
- **Documentation is part of the change**: a feature is not finished until
  the documents that describe it are right.

## Adding a mapper

A mapper is the cartridge's hardware: bank switching, mirroring, sometimes
an IRQ counter. The interface is `Mapper` in
`core/src/cutenes/mappers/mapper.hpp`; how it fits the core is in
[`core-internals.md`](core-internals.md), "Cartridges and mappers".

1. **Read the board's NESdev wiki page** and note the register map, the
   banking modes, the mirroring control, any IRQ, and the power-on state.
   If revisions of the board behave differently, note how the NES 2.0
   submapper tells them apart.
2. **Write unit tests first**, in `tests/unit/test_mappers.cpp`. Build a
   cartridge in memory, write the registers, and check what the CPU and PPU
   buses read, with expected values from the wiki page. Name each test for
   the behavior (`Mmc1.FifthWriteCommitsNotBitZeroOfEachWrite`).
3. **Find a test ROM** that exercises the board: a Holy Mapperel ROM
   (`tests/playtest/mapperel.txt`), a board-specific suite, or a homebrew
   game, with its license recorded in `third_party/NOTICE.md`. Add it to the
   corpus with its own verdict as the expectation
   ([`test-corpus.md`](test-corpus.md)).
4. **Implement the board** in `core/src/cutenes/mappers/` (a new file, or
   with its family, as `discrete.cpp` does):
   - a class deriving from `Mapper`, overriding `cpuRead`/`cpuWrite` for
     its registers and banks, and `chrRead`/`chrWrite` and `mirroring()` as
     the board needs;
   - `saveState`/`loadState` for every register and counter, so a save
     state resumes the board exactly;
   - comments citing the wiki page for each non-obvious behavior.
5. **Register it**: declare its `make…` function and add a `case` for its
   mapper number in `createMapper` (`mappers/nrom.cpp`), and add the source
   file to `core/CMakeLists.txt`.
6. **Run every test**, not only the new ones: `ctest --test-dir build -j8
   -LE known-failing`.
7. **Document it**: the support table in
   [`using-the-core.md`](using-the-core.md), and
   [`mappers.md`](mappers.md) if it has revision-dependent behavior.

## Adding an automation command

Commands are what MCP clients, scripts and the GUI's menus all call. The
`core.*` and `session.*` commands live in `cutenes-api`; how they are
dispatched is in [`architecture.md`](architecture.md).

1. **Specify it** in feature 002's
   [`command-catalog.md`](../specs/002-ai-mcp-server/contracts/command-catalog.md):
   its name (`<namespace>.<verb>_<object>`), what it does, its input and
   output, its errors, and whether it changes state (`mutating`) or needs a
   loaded game (`needsRom`).
2. **Write the tests first**, in `tests/api/`, usually
   `test_core_commands.cpp`: call the command through the registry, and
   check its result against the contract, and its errors. A failing call
   must change no state (`test_errors.cpp` shows how).
3. **Register it** with `addCommand` in `api/src/core_commands.cpp` (or
   `session_commands.cpp`): the name, a one-line description, the input and
   output JSON Schemas (built with the `s::` helpers), the `mutating` and
   `needsRom` flags, and the handler. The handler runs on the emulation
   thread, so it can use the `Machine` directly.
4. **Bump the surface version** in `api/include/cutenes/api/registry.hpp`:
   `kSurfaceMinor` for a new command or optional field, `kSurfaceMajor` for
   a removal, a rename, or a change of type or meaning.
5. **Regenerate the frozen catalog**:
   `CUTENES_UPDATE_GOLDEN=1 ctest --test-dir build -R ApiCatalogGolden`,
   and commit the changed `tests/api/catalog.golden.json`. Without the
   version bump and this file, the build fails, so no schema can change by
   accident.
6. **Check it through MCP**: the command appears as a tool automatically.
   Drive it from an MCP client against `cutenes-server`
   ([`automation.md`](automation.md)).
7. **Document it**: the command count in [`automation.md`](automation.md)
   if it changed, and the version.

## Adding a GUI feature

Every user-facing action in the desktop application is a menu item, a
`gui.*` command, or both, and the two must agree (automation parity).
`tests/gui/test_audit.cpp` enforces it against feature 003's
[`menus-and-commands.md`](../specs/003-qt-frontend/contracts/menus-and-commands.md).

1. **Specify it**: a scenario in feature 003's spec, and a row in the menus
   contract (item, command, arguments, shortcut, and whether it needs a
   game). A setting goes in its settings group.
2. **Write the tests first** in `tests/gui/`. They run the real
   application objects offscreen (`QT_QPA_PLATFORM=offscreen`, SDL's dummy
   audio). Drive the feature through its `gui.*` command and check the
   result the user would see: the window's state, a file written, the
   frame shown.
3. **Register the command** in `app/host/gui_host.cpp` with `add(registry,
   ...)`: name, description, schemas, flags and handler. The handler runs
   on the emulation thread: machine work runs inline, and widget work is
   posted to the GUI thread (`QMetaObject::invokeMethod`, queued). A
   command that opens a dialog returns once it is shown.
4. **Add the menu item** in `app/mainwindow.cpp` (`menu.item(tr("…"),
   "gui.your_command")`). The action calls the command, so a click and an
   MCP call run the same code.
5. **Run the audit** (`ctest --test-dir build -L gui`): the menus, the
   contract and the catalog must still agree.
6. **Check it in the real application** through the MCP
   (`cutenes-server --attach`), then add the manual steps that only a
   person can check (look, sound, feel) to
   `specs/003-qt-frontend/manual-test-queue.md`.
7. **Document it** in the [user guide](user-guide.md), which is also the
   in-application Help. The keyboard shortcut list there comes from the
   contract.

## Before opening a pull request

- Every test passes locally, with nothing new in `known-failing`.
- Every new constant or behavior cites its source.
- The documents above are current, including the test counts in
  [`building-and-testing.md`](building-and-testing.md) if they changed.
- A bug you found and did not fix has an issue.
