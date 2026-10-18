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

## No other third-party code is redistributed in this repository.

GoogleTest (BSD-3-Clause) is fetched at configure time for standalone test
builds only and is never shipped in any artifact.
