#!/usr/bin/env python3
"""Require a new emulation revision when a pinned hash changes (#166).

The core's emulation revision (core/include/cutenes/version.hpp,
`emulationRevision`) is the number movies record, so playback can warn when
the same input may no longer give the same output. Pinned hashes are the
project's record of that output, so a pin that changes value means the
emulation changed, and the revision has to rise with it.

What counts as a pin that moved, comparing a base commit with the working
tree:
- tests/golden/baseline.txt: a `golden: <name> ...` line whose values
  differ for the same name.
- tests/playtest/*.txt: an entry whose steps are otherwise unchanged but
  whose `expect-hash` values differ. When an entry's steps change too, the
  test changed, and its new hashes say nothing about the emulation.

Added or removed pins are not moves. Behavior that no pin covers is not
seen here, which is why version.hpp also asks for the revision to rise for
such fixes.

    tools/check_emulation_revision.py --base origin/main
    tools/check_emulation_revision.py --self-test

Exit status: 0 when nothing moved or the revision rose, 1 when a pin moved
without a new revision, 2 on a usage or git error.
"""

import argparse
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
VERSION_HPP = "core/include/cutenes/version.hpp"
GOLDEN = "tests/golden/baseline.txt"
PLAYTEST_DIR = "tests/playtest"

REVISION_RE = re.compile(r"emulationRevision\s*=\s*(\d+)\s*;")


def revision(text):
    """The emulationRevision in a version.hpp text; 0 before it existed."""
    m = REVISION_RE.search(text or "")
    return int(m.group(1)) if m else 0


def golden_pins(text):
    """{name: the rest of its line} for each `golden:` line."""
    pins = {}
    for line in (text or "").splitlines():
        parts = line.split(None, 2)
        if len(parts) >= 2 and parts[0] == "golden:":
            pins[parts[1]] = " ".join(line.split()[2:])
    return pins


def playtest_entries(text):
    """{entry: (steps without hashes, [hashes])} for a playtest list."""
    entries = {}
    name = None
    steps, hashes = [], []
    for raw in (text or "").splitlines():
        line = raw.split("#", 1)[0].split()
        if not line:
            continue
        if line[0] == "entry" and len(line) > 1:
            name, steps, hashes = line[1], [], []
        elif line[0] == "end" and name is not None:
            entries[name] = (tuple(steps), hashes)
            name = None
        elif name is not None:
            if line[0] == "expect-hash" and len(line) > 1:
                hashes.append(line[1].lower())
                steps.append("expect-hash")  # where it sits still counts
            else:
                steps.append(" ".join(line))
    return entries


def moved_golden(base, head):
    return [f"{GOLDEN}: {n}: {base[n]} -> {head[n]}"
            for n in sorted(base.keys() & head.keys()) if base[n] != head[n]]


def moved_playtest(path, base, head):
    out = []
    for n in sorted(base.keys() & head.keys()):
        (bsteps, bhashes), (hsteps, hhashes) = base[n], head[n]
        if bsteps == hsteps and bhashes != hhashes:
            out.append(f"{path}: {n}: {' '.join(bhashes)} -> {' '.join(hhashes)}")
    return out


def check(base_files, head_files):
    """Returns (moves, base revision, head revision); files map path -> text."""
    moves = moved_golden(golden_pins(base_files.get(GOLDEN)), golden_pins(head_files.get(GOLDEN)))
    for path in sorted(p for p in head_files if p.startswith(PLAYTEST_DIR + "/")):
        moves += moved_playtest(path, playtest_entries(base_files.get(path)),
                                playtest_entries(head_files.get(path)))
    return moves, revision(base_files.get(VERSION_HPP)), revision(head_files.get(VERSION_HPP))


def git_show(ref, path):
    r = subprocess.run(["git", "show", f"{ref}:{path}"], cwd=ROOT, capture_output=True, text=True)
    return r.stdout if r.returncode == 0 else None


def tracked_files():
    paths = [VERSION_HPP, GOLDEN]
    paths += sorted(str(p.relative_to(ROOT)).replace("\\", "/") for p in (ROOT / PLAYTEST_DIR).glob("*.txt"))
    return paths


def self_test():
    hpp = "inline constexpr std::uint32_t emulationRevision = {};\n"
    golden = "golden: a last=1111 audio=5\ngolden: b last=2222 audio=5\n"
    play = "entry e\nrom x.nes\nrun 10 none\nexpect-hash aaaa0000\nend\n"
    pt = PLAYTEST_DIR + "/p.txt"

    def files(rev, g, p):
        return {VERSION_HPP: hpp.format(rev), GOLDEN: g, pt: p}

    cases = [
        ("nothing moved", files(1, golden, play), files(1, golden, play), False),
        ("a golden pin moved", files(1, golden, play),
         files(1, golden.replace("2222", "3333"), play), True),
        ("a golden pin moved with a new revision", files(1, golden, play),
         files(2, golden.replace("2222", "3333"), play), False),
        ("a golden pin added", files(1, golden, play),
         files(1, golden + "golden: c last=4444 audio=5\n", play), False),
        ("a playtest hash moved", files(1, golden, play),
         files(1, golden, play.replace("aaaa0000", "bbbb0000")), True),
        ("a playtest hash moved, comment and case aside", files(1, golden, play),
         files(1, golden, play.replace("aaaa0000", "AAAA0000  # viewed")), False),
        ("a playtest entry rewritten", files(1, golden, play),
         files(1, golden, play.replace("run 10", "run 20").replace("aaaa0000", "bbbb0000")), False),
        ("the revision went down", files(2, golden, play),
         files(1, golden.replace("2222", "3333"), play), True),
        ("before the revision existed", {GOLDEN: golden, pt: play},
         files(1, golden.replace("2222", "3333"), play), False),
    ]
    failed = 0
    for name, base, head, should_refuse in cases:
        moves, b, h = check(base, head)
        if (bool(moves) and h <= b) != should_refuse:
            failed += 1
            print(f"FAIL: {name}: moves={moves} base={b} head={h}")
    print(f"self-test: {len(cases) - failed}/{len(cases)} passed")
    return 1 if failed else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--base", help="the commit to compare with, such as origin/main")
    parser.add_argument("--self-test", action="store_true", help="check this script's own rules")
    args = parser.parse_args()
    if args.self_test:
        return self_test()
    if not args.base:
        parser.error("give --base or --self-test")
    if subprocess.run(["git", "rev-parse", "--verify", "--quiet", args.base + "^{commit}"],
                      cwd=ROOT, capture_output=True).returncode != 0:
        print(f"unknown commit: {args.base}", file=sys.stderr)
        return 2
    paths = tracked_files()
    base = {p: git_show(args.base, p) for p in paths}
    head = {p: (ROOT / p).read_text(encoding="utf-8") for p in paths if (ROOT / p).exists()}
    moves, b, h = check(base, head)
    if not moves:
        print(f"No pinned hash moved (emulation revision {h}).")
        return 0
    print("Pinned hashes that moved:")
    for m in moves:
        print("  " + m)
    if h > b:
        print(f"The emulation revision rose from {b} to {h}.")
        return 0
    print(f"\nThe emulation revision is still {h}. Raise emulationRevision in {VERSION_HPP}:"
          " movies recorded before this change may play back differently (#166).")
    return 1


if __name__ == "__main__":
    sys.exit(main())
