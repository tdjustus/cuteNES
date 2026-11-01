# Source control strategy

How code gets from an idea to `main`, how releases are cut, and how known
bugs and planned features are tracked. This is the canonical description;
the constitution, `README.md` and `CLAUDE.md` point here.

## Phases

| Phase | Integration branch | Releases are cut by |
|---|---|---|
| **Pre-1.0** (now) | `main` | tagging `main` (`v<version>` for the app, `core-v<version>` for the core; see [Versions and tags](#versions-and-tags)) |
| **1.0 and later** | `development` (long-lived, never deleted) | merging `development` into `main`, then tagging `main` |

The `development` branch does not exist until 1.0 ships. Version 1.0 is
reached when **every roadmapped feature is implemented and every known bug
is resolved**, meaning the GitHub milestone `1.0` has no open issues (see
[Tracking work](#tracking-work-the-10-milestone)).

## Pre-1.0 workflow

1. **Branch from fresh `main`**: `git fetch` and branch from `origin/main`
   as `<kind>/<slug>`, where kind is `feature`, `fix`, `docs`, `test` or
   `chore`. Commit messages use the matching prefix (`feat(...)`,
   `fix(...)`, `docs`, `test`, `chore`).
2. **One branch at a time.** Do not start the next branch, or continue
   development, until the previous branch's pull request has merged. Do not
   stack branches on one another. Why: `main` requires branches to be up to
   date before merging, so each merge leaves every other open branch behind,
   and each update costs a full CI run.
3. **One logical change per branch.** Keep it small enough to review.
4. **Tests first for core behavior** (constitution, Principle III): write
   the test, watch it fail, then fix.
5. **Open a PR into `main`.** If the PR resolves tracked issues, put
   `Fixes #N` in its description so they close on merge, but only when the
   PR fully resolves the issue. Otherwise use `Refs #N`, in every commit
   message too (a closing keyword anywhere in the history closes it). Never
   close an unresolved issue; postpone it with a label or later milestone.
6. **Keep the branch current by merging `main` into it** (no rebase). Check
   first whether the remote branch already contains `main`; the "Update
   branch" button in GitHub does the same thing.
7. **Merge once checks pass.** Since 2026-10-29 the maintainer has
   authorized Claude Code to merge its own PRs after every required check
   (`ci-ok`) has passed. Never merge while a check is red or pending, never
   bypass protection, and if the merge is refused, report it instead of
   retrying. The maintainer may still merge any PR.
8. **Merge commits only** (squash and rebase are disabled) so each change
   keeps its own commit messages. The head branch is deleted automatically
   on merge; delete the local copy and pull `main`.
9. Never push directly to `main`; hotfixes follow the same path on `fix/`.

## Repository protections

- `main`: pull request required (0 approvals), required status check
  `ci-ok`, branch must be up to date, no force-push, no deletion. Admin
  enforcement is off so the maintainer can still merge their own PRs; it also
  means the owner account's credentials *can* push to `main` directly, which
  must never be done (this happened once, 2026-10-30, commit `fbade9c`).
- Allowed merge method: merge commit. Delete-branch-on-merge: on.

## CI

Defined in `.github/workflows/build.yml`.

- **Triggers:** `pull_request`, tags `v*` and `core-v*`, a weekly schedule, and
  manual dispatch. There is deliberately no run on pushes to branches or on
  `main` after a merge: a PR is tested up to date with `main`, so a
  post-merge run would repeat an already-tested tree. The weekly run catches
  runner-image drift; the tag run gates releases.
- **Jobs:** Release on Linux, macOS and Windows; Windows Debug; one Linux
  ASan + UBSan job (`-O1`, assertions on). The Linux Release job also runs
  the speed gate (`rom_harness bench`, floor 1.0x real time; see
  `docs/test-corpus.md`).
- **What each runs:** the Release jobs run the full suite on every PR. The slow
  instrumented jobs (Windows Debug and ASan + UBSan) run only the fast groups
  (`unit`, `tier1` including the golden hashes, `behavior`) on PRs; the
  on-screen-verdict groups (`playtest`, `tier2`, `corpus`) take about five times
  as long and mostly re-exercise the same code. Tags (`v*`, `core-v*`) and the weekly
  schedule run everything under every job, so the full instrumented suite still
  gates a release.
- **`ci-ok`** aggregates them and is the only required check. Docs-only PRs
  (no change under `core/`, `tests/`, `third_party/`, `CMakeLists.txt`,
  `.gitmodules` or `.github/`) skip the build, and `ci-ok` still passes.
- **Superseded runs** are cancelled when a PR is pushed again.
- Tests labelled `known-failing` are excluded from CI. Use them only for a
  documented emulator discrepancy, and file an issue for it.
- Run locally: `cmake -S . -B build && cmake --build build -j` then
  `ctest --test-dir build -j8 -LE known-failing`. Playtest entries that need
  gitignored commercial ROMs (`roms-local/`) report *Skipped*, not Passed.

## Tracking work: the 1.0 milestone

The GitHub milestone **1.0** is the source of truth for "known bugs and
roadmapped features". Labels: `bug`, `feature`, `test`, `ci`,
`documentation`.

- Every known bug that is not fixed in the current PR gets an issue in the
  milestone immediately, with cause and reproduction. Do not leave bugs only
  in a PR description or a commit message.
- Every roadmapped feature, and any deferred test or documentation work,
  gets an issue in the milestone.
- Check the open milestone issues at the start of a work session.
- Scope is set by the maintainer: a suggested feature is not a milestone
  item until the maintainer agrees to it.

## Versions and tags

Two things are versioned, each with its own semantic version and tag, because
each has its own users:

| What | Version lives in | Tag | Who reads it |
|---|---|---|---|
| The application (GUI, servers, packages) | `project(cutenes VERSION ...)` in the top-level `CMakeLists.txt`: About, the macOS bundle, `User-Agent`, the update check and the package names all take it from there | `v<major>.<minor>.<patch>`, or `v<...>-alpha.<n>` / `-beta.<n>` / `-rc.<n>` for a prerelease | people downloading a package, and the self-update (research R16) |
| The emulation core (the library) | `core/include/cutenes/version.hpp` and `core/src/cutenes/version.cpp` (and `tests/unit/test_version.cpp`) | `core-v<major>.<minor>.<patch>` | projects embedding the core (README "Using the core") |

- **Why separate.** The core's version is written into every save state, and
  until 1.0 a state loads only into the exact core version that wrote it
  (`IncompatibleCoreVersion`). Raising it makes every user's save states
  unusable, so it changes only when the core itself is released, not with
  every app release.
- **Which part to raise.** Pre-1.0, a release with new features raises the
  minor version, and a release with fixes only raises the patch version.
  From 1.0 on, an incompatible change raises the major version.
- **Prereleases.** An alpha, beta or release candidate is tagged with the
  `-<kind>.<n>` suffix on the version it leads to, and published as a
  GitHub prerelease. The self-update never offers a prerelease (R16), so
  testers opt in by downloading it by hand.
- History: the first app alpha predates this scheme and is tagged
  `core-v0.1.0-alpha.1`.

## Publishing a release

Each step publishes something, so the maintainer asks for the release
before any of it is done.

1. On a `chore/version-<version>` branch, raise the version (above) and
   anything in the docs that names it, run the full suite, and merge the
   pull request as usual.
2. Tag the merge on `main` (`git tag -a v<version> -m ...`, then
   `git push origin v<version>`). The tag's CI runs the full suite on every
   OS and the `package` and `package-sums` jobs (`docs/packaging.md`).
3. When that run is green, download its artifacts
   (`gh run download <run> -D <empty folder>`) and check each package
   against `SHA256SUMS` (`shasum -a 256 -c SHA256SUMS`).
4. Create the release from the tag with every package and `SHA256SUMS`
   attached (`gh release create v<version> <files> --title ... --notes-file
   ...`, plus `--prerelease` for an alpha, beta or release candidate). The
   notes say what changed since the last release, what is known not to
   work, how to open an unsigned build, and the licenses.

## Releasing 1.0

When the milestone has no open issues:

1. Confirm `README.md` and this document are current and `main`'s CI is green
   on all platforms.
2. Create the long-lived `development` branch from `main`.
3. Protect `development`: pull request required, no deletion. (Branch
   protection also stops delete-branch-on-merge from removing it when a
   release PR merges.)
4. Tag `v1.0.0` (and `core-v1.0.0`) from `main` once its CI is green, and
   publish the release as in [Publishing a release](#publishing-a-release).

These steps change repository settings and publish a release, so the
maintainer approves each one.

## After 1.0

- Feature and fix branches start from `development` and merge into it, one
  at a time, with the same rules as above.
- A release is a pull request that merges `development` into `main`. It runs
  CI once more as the release gate (one extra run per release, not per
  change). Tag `v<version>` (and `core-v<version>` when the core changed) on
  `main` after that CI is green.
- **After every release, merge `main` back into `development`.** The merge
  commit on `main` is not on `development`, and without the back-merge the
  "must be up to date" rule blocks the next release PR.
- **Hotfixes:** branch from `main`, merge to `main`, tag, then merge `main`
  into `development`.
- `main` always equals the latest release. Tags are cut from `main` only.
