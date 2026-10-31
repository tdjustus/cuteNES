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

## nlohmann/json v3.12.0 (Niels Lohmann) — fetched at configure time

- **Origin**: https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
  (release 2025-04-11), SHA-256
  `42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa`,
  pinned in `api/CMakeLists.txt`
- **License**: MIT
- **Used by**: `cutenes-api` (feature 002). Not vendored; not used by the core

## third_party/stb — stb_image_write.h v1.16 (Sean Barrett)

- **Origin**: https://github.com/nothings/stb, file `stb_image_write.h` at
  commit `1ee679ca2ef753a528db5ba6801e1067b40481b8`, SHA-256
  `cbd5f0ad7a9cf4468affb36354a1d2338034f2c12473cf1a8e32053cb6914a05`
- **License**: dual public domain (Unlicense) / MIT; the text is at the end
  of the vendored file
- **Used by**: `cutenes-api` PNG screenshots (`core.screen`), in memory only
  (`STBI_WRITE_NO_STDIO`)

## cpp-httplib v0.61.0 (Yuji Hirose) — fetched at configure time

- **Origin**: https://github.com/yhirose/cpp-httplib/archive/refs/tags/v0.61.0.tar.gz
  (release 2026-10-14), SHA-256
  `f03bdb0bee4911d6db0335bc103f0f441ae70c527e993c1f4b3f177586b042d3`,
  pinned in `api/CMakeLists.txt`. Only the header is used (no OpenSSL)
- **License**: MIT
- **Used by**: the API HTTP listener, its tests, and later the MCP
  Streamable HTTP listener and the `--attach` bridge

## No other third-party code is redistributed in this repository.

GoogleTest (BSD-3-Clause) is fetched at configure time for standalone test
builds only and is never shipped in any artifact.
