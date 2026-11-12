# Development process

How work on cuteNES is planned, built, checked and released. The rules are
in the [constitution](../.specify/memory/constitution.md) and
[`source-control.md`](source-control.md); this document puts them in
order, from an idea to a release. The recipes for common changes are in
[`contributing.md`](contributing.md).

## 1. Track it

Every known bug and every planned feature is a GitHub issue in the
milestone of the next release, currently
[**1.0**](https://github.com/tdjustus/cuteNES/milestone/1). Features
planned for later releases are in their own milestones, laid out in
[`roadmap.md`](roadmap.md). The issue comes first, before the work. A bug found while working on something else gets its
own issue rather than a silent fix in an unrelated pull request. That
includes one fixed straight away, a CI break, a test race, or a failure
that could not be reproduced. An issue is closed when the work that
resolves it merges; until then it stays open, or moves to a later
milestone. Work that is merged and waits only on hands-on checks is
closed, and its checks go in the manual test pass (#190). The full rule is
in [`source-control.md`](source-control.md), "Tracking work".

**General idea:** one list holds everything known to be wrong or missing,
so "are we done?" has an answer, and nothing known is ever just
remembered.

## 2. Specify it (features)

A feature starts as a specification, never as code. cuteNES uses
[Spec Kit](https://github.com/github/spec-kit), whose steps are skills in
`.claude/skills/`:

| Step | Produces | What it settles |
|---|---|---|
| `speckit-specify` | `spec.md` | what the feature does and why: user stories, functional requirements (FR-…) and success criteria (SC-…), with no implementation |
| `speckit-clarify` | answers in `spec.md` | the questions the spec left open, asked of the maintainer |
| `speckit-plan` | `plan.md`, `research.md`, `data-model.md`, `contracts/` | how: the design, each decision with its alternatives and sources, and the contracts tests are written from |
| `speckit-tasks` | `tasks.md` | the work, in dependency order, each task tied to its requirements, tests first |
| `speckit-implement` | code | the tasks, one branch at a time |
| `speckit-analyze`, `speckit-converge` | reports, new tasks | whether the spec, plan, tasks and code still agree |

The documents live in `specs/<number>-<name>/`. A change of scope is an
amendment to the spec, not a silent addition. Bug fixes skip this: they
start from the issue and a failing test.

## 3. Branch

- One branch at a time, from a fresh `origin/main`, named
  `<kind>/<slug>` (`feature`, `fix`, `docs`, `test`, `chore`).
- The previous pull request is merged before the next branch starts.
  Branches are never stacked.
- A branch is updated by merging `main` into it, never by rebasing or
  force-pushing.

Why one at a time: each change is reviewed and tested against the `main`
it will land on, and nothing waits on an unmerged dependency. The full
strategy is in [`source-control.md`](source-control.md).

## 4. Build it test-first

For a change to emulation behavior:

1. Write the test from the hardware documentation, a test ROM's own
   verdict, or the contract.
2. Run it and see it fail, for the reason you expect.
3. Make the change.
4. Run the whole suite. A regression in a test ROM blocks the change,
   whatever the feature is worth.

When a fix moves a pinned frame hash, find out why before re-pinning:
compare against the previous build and say in the commit what moved and
why the new value is right ([`test-corpus.md`](test-corpus.md)).

A moved pin means the emulation's output changed, so the same pull request
raises `emulationRevision` in `core/include/cutenes/version.hpp`. Movies
record that number and warn when it differs, because an older movie may
now replay differently (#166). CI's `emulation-revision` job refuses a pull
request in which a golden or playtest hash changed value and the revision
did not rise (`tools/check_emulation_revision.py`; run it locally with
`--base origin/main`). Raise it too for a fix that changes behavior no pin
covers, such as PAL-only timing; the check cannot see those.

## 5. Check it in the real program

Automated tests run offscreen. A change to the desktop application is also
checked in the running application, through its own MCP server: an agent
(Claude Code, attached with `cutenes-server --attach`) opens a game,
drives the feature through its `gui.*` commands, and reads back the
state, the frame and the files it wrote.

What only a person can judge (how it looks, sounds and feels) is added to
`specs/003-qt-frontend/manual-test-queue.md`. The maintainer runs that
queue in one pass when the frontend's tasks are done, together with the
phase checklists in feature 003's `quickstart.md`.

**General idea:** an application that exposes every action as a command
can be tested end to end by a program, which leaves people the parts that
need a person.

## 6. Open a pull request

- The description says what changed and why, what was verified and how,
  and what was not.
- It names its issues (the pull request template lists them). `Fixes #N`
  only when the pull request fully resolves the issue; `Refs #N` otherwise.
  A closing keyword closes the issue on merge.
- After the merge, each issue it touched is closed if done, or given a
  status comment if not.
- CI runs on the pull request. Its single required check, `ci-ok`, passes
  only when every build and test job succeeded or was skipped (docs-only
  changes skip the build).
- It is merged with a merge commit once `ci-ok` passes and the branch is up
  to date with `main`. Never with a red or pending check, and never by
  overriding protection.

## 7. Release

Releases are tagged from `main`. Before 1.0, each is an alpha or beta, with
the count running across the project (alpha 4 was `v0.3.1-alpha.4`; the
first beta will be `v0.9.0-beta.1`). An alpha may ship with manual checks
still queued; a beta or a production release waits until every manual test
has passed (on macOS; Windows and Linux are best effort, checked by CI). The tag's CI builds the packages for all three platforms and their
`SHA256SUMS`; the packages are downloaded, checked against the sums, and
published with notes that say what changed, what is known not to work, how
to open an unsigned build, and the licenses. Every line of the notes is
checked against the code or the previous release before publishing. The
steps are in [`source-control.md`](source-control.md), "Publishing a
release".

Version 1.0 ships when its milestone has no open issues and every manual
test has passed.

## Working with Claude Code

Much of cuteNES has been written with Claude Code. The project's
instructions for it are in `CLAUDE.md`, which points to the same rules as
this document. It follows the same process as anyone: an issue (filed first, closed on merge), a branch,
tests first, a pull request, and a merge only on a green `ci-ok`. It checks
GUI changes through the MCP tools as in step 5, and asks the maintainer for
decisions that are theirs: scope, policy, and anything that publishes.
