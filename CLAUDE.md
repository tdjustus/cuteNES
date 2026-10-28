# cuteNES: instructions for Claude Code

Cross-platform NES emulator: a headless, deterministic C++20 core, with a
planned MCP server and Qt 6 frontend. Read the constitution
(`.specify/memory/constitution.md`) before changing behavior, and follow
`docs/source-control.md` for all git and GitHub work. It is the canonical
strategy; the rules below are the ones that must never be missed.

## Source control (details in `docs/source-control.md`)

- **One branch at a time.** Do not start a new branch or continue
  development until the previous PR has merged. Never stack branches.
- Branch from fresh `origin/main` as `<kind>/<slug>` (`feature`, `fix`,
  `docs`, `test`, `chore`). Update a branch by merging `main` into it, never
  by rebasing or force-pushing.
- **Never merge a PR.** Open it, keep it current, and tell the maintainer it
  is ready. Merge commits only.
- No `development` branch exists until v1.0. Do not create it, tag a
  release, or change branch protection without the maintainer's approval.
- **Known bugs and features live in the GitHub milestone `1.0`.** At the
  start of a session, list its open issues. File an issue for any bug you
  find but do not fix in the current PR. Put `Fixes #N` in PRs. When the
  milestone is empty, tell the maintainer and propose the release steps in
  `docs/source-control.md`.

## Build and test

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build -j8 -LE known-failing
```

- Core behavior changes are test-first: write the failing test, then fix.
- Test expectations come from hardware documentation or the ROM's own
  documented behavior, never from the emulator's current output. A test that
  only asserts "the screen changed" proves very little.
- Commercial ROMs live in gitignored `roms-local/`; never commit them.
