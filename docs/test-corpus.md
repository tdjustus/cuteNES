# Test-ROM corpus: what is run and what is not

The vendored test ROMs live in `tests/roms/` (the `christopherpow/nes-test-roms`
submodule) and `third_party/holy-mapperel/`. Every folder is accounted for here.
Expected results come from each ROM's own documentation, never from this
emulator's output (constitution, Principle III).

## How ROMs are checked

| Mechanism | Where | Used for |
|---|---|---|
| Tier 1: `$6000` protocol | `TIER1_ROMS` in `tests/CMakeLists.txt` | Self-checking blargg suites; exit code is the verdict |
| Playtest `expect-text` | `tests/playtest/*.txt` | ROMs that print a verdict on screen (`PASSED`, `$01`, board names) |
| Golden hash | `tests/golden/` | Cross-platform determinism for a few ROMs, each verified by eye |
| Known-failing | `*-known-failing.txt` (label `known-failing`) | A documented emulator discrepancy; excluded from CI, tracked by an issue |

Run everything CI runs with `ctest --test-dir build -j8 -LE known-failing`; the
known-failing set with `ctest -L known-failing`.

## Pinned screen hashes

`expect-hash <8 hex>` in a playtest entry pins the exact frame (FNV-1a of the
XRGB pixels) at that point. It is used for the commercial-ROM entries, which
run only on a machine that has the ROMs in `roms-local/` (they are gitignored
and skipped elsewhere), and for nestest. A scripted entry that only asserts
"the screen changed" proves very little: a scrambled screen satisfies it, which
is how the MMC3 CHR bug went unnoticed.

**A pin is only as good as the frame it records, so view the frame first.**
To pin or re-pin an entry:

1. `build/tests/rom_harness playtest --list tests/playtest/playtest.txt --entry NAME --shots DIR`
   writes one PPM per `run` step, named `NAME-<step>-<hash>.ppm`.
2. Look at the frame (convert the PPM to PNG, or open it in an image viewer)
   and confirm it shows what the entry claims.
3. Put `expect-hash <hash>` after that `run` step.

An intentional emulator change that alters pixels (a mapper fix, a PPU
change) needs the affected pins re-viewed and re-pinned the same way; an
unexplained change is a regression.

Entry names say what the frame shows. If a script does not reach the screen its
name suggests, the entry is named for where it actually ends (for example
`zelda-2-boot-reaches-file-registration`) and the gap is recorded in the entry
comment.

## Folders added by the corpus triage

| Folder | ROMs | Disposition |
|---|---|---|
| `blargg_apu_2005.07.30` | 11 | 01-09 pass (`$01` = passed, per `tests.txt`): `corpus.txt`. 10 and 11 fail: known-failing, #43 |
| `branch_timing_tests` | 3 | 1 and 2 pass: `corpus.txt`. 3 fails: known-failing, #41 |
| `vbl_nmi_timing` | 7 | 1-6 pass: `corpus.txt`. 7 fails: known-failing, #42 (the newer `ppu_vbl_nmi` set passes; the old ROM may be superseded) |
| `pal_apu_tests` | 10 | Run as PAL (`region pal`). 01-03 pass: `corpus.txt`. 04-08, 10, 11 fail: known-failing, #44 (APU tables are NTSC-only) |
| `cpu_dummy_writes` | 2 | Tier 1 |
| `instr_misc` | 5 | The four `rom_singles` are tier 1. `instr_misc.nes` is the same four tests combined; not registered |

## Superseded older versions (pass, deliberately not registered)

`instr_test-v3` (17 ROMs), `nes_instr_test` (11) and `blargg_nes_cpu_test5` (2)
are older revisions of the instruction tests that `instr_test-v5`
(`official_only`, `all_instrs`, tier 1) already covers. They pass today; they
are skipped only to save CI time, mostly under the sanitizer job.

## Not pass/fail tests (no automated verdict)

| Folder | Why |
|---|---|
| `apu_mixer` (4) | Audible-only: each beeps, plays a tone cancelled by an inverse DMC waveform, and beeps again. Their protocol exit code is not a mixer verdict. A numerical check ("near silence between the beeps") is possible but not built |
| `240pee` (2) | A video test suite (menus). The BNROM build is pinned as golden `bnrom_240pee` (verified by eye); the NROM build is the same program |
| `dpcmletterbox`, `nes15-1.0.0`, `nmi_sync`, `ny2011`, `scrolltest`, `spritecans-2011`, `stomper`, `tutor`, `tvpassfail`, `window5` | Demos and visual tests with no documented pass/fail screen in this repository |
| `PaddleTest3`, `vaus-test` | Need a paddle controller, which the core does not model |
| `volume_tests`, `dmc_tests` (4) | Audible or timing-by-ear tests with no on-screen verdict |
| `scanline-a1` | A mid-scanline PPU write test whose expected picture is not documented here; needs research before it can be asserted |
| `nrom368` | Tests a 368K NROM image; the expected result is not documented here; needs research |

## Folders with no ROM files

`apu_mixer_recordings`, `fdsirqtests`, `soundtest` and `stars_se` contain only
sources, recordings or documents and no `.nes` file, so there is nothing to run.

## Already covered before the triage

`instr_test-v5`, `instr_timing`, `ppu_vbl_nmi`, `apu_test`, `apu_reset`,
`cpu_interrupts_v2`, `cpu_reset`, `oam_read`, `oam_stress`, `sprdma_and_dmc_dma`,
`ppu_open_bus`, `cpu_exec_space`, `ppu_read_buffer`, `mmc3_test_2` (tier 1);
`mmc3_irq_tests`, `blargg_ppu_tests`, `sprite_hit_tests`, `sprite_overflow_tests`,
`dmc_dma_during_read4`, `cpu_dummy_reads`, `cpu_timing_test6`, `read_joy3`,
`mmc3_test`, `mmc5test*`, `exram` (playtests); Holy Mapperel (all 22, in
`playtest/mapperel.txt`); and the golden-hash set.
