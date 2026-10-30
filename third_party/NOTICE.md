# Third-Party Provenance

Project code is MIT (see ../LICENSE). This file records the origin and
license status of every redistributed third-party artifact, per the
constitution's licensing rules (research.md D11).

## tests/roms — christopherpow/nes-test-roms (git submodule)

- **Origin**: https://github.com/christopherpow/nes-test-roms
- **Contents**: blargg (Shay Green) test suites, nestest (kevtris), and
  other community test ROMs
- **License status**: NO EXPLICIT LICENSE. Published as free downloads by
  their authors; redistributed for years by the emulator community; the
  NESdev wiki endorses this archive. NOT covered by this project's MIT
  license. Kept out of the project tree via a pinned git submodule.
- **Authors**: Shay Green <gblargg@gmail.com> (blargg suites), Kevin Horton
  (nestest). Per-suite readme.txt files inside the submodule carry
  attribution.

## third_party/holy-mapperel — Holy Mapperel (tepples / Damian Yerrick)

- **Origin**: https://github.com/pinobatch/holy-mapperel
- **License**: zlib License (vendored copy accompanies the files)
- **Status**: ROM binaries to be vendored here when the tier-2 mapper
  verification lands (task T058); this entry records the provenance now.

## third_party/test28 — test28 0.04, Action 53 mapper test (Damian Yerrick)

- **Origin**: `test28-0.04.zip`, attached to nesdev forum post
  https://forums.nesdev.org/viewtopic.php?p=215345 (2018-03-15)
- **Contents**: `test28.nes` (4 Mbit build, SHA-256
  `b6f6715f3003d401b81a721abef74156756c412262a8acb73da3facbdcc70e6b`),
  `README.md`, `CHANGES.txt`
- **License**: all-permissive ("Copying and distribution of this file, with
  or without modification, are permitted in any medium without royalty
  provided the copyright notice and this notice are preserved"); the notice
  is in the vendored `README.md` ("Legal")
- **Why vendored**: the `tests/roms/other/test28.nes` in the submodule is a
  2012 build older than 0.01, whose bus-conflict check was wrong (CHANGES.txt:
  "0.01: fixed bus conflict detection")

## No other third-party code is redistributed in this repository.

GoogleTest (BSD-3-Clause) is fetched at configure time for standalone test
builds only and is never shipped in any artifact.
