# Contract: Core additions for feature 003 (feature 001 amendment)

**Feature**: 003 (phase G6) | **Authority**: spec "Core additions", clarification 2026-10-30; research R10

These are appended to `specs/001-emulation-core/contracts/core-api.md`
and to its capability table. Each is test-first under the core's rules,
with expectations taken from the cited nesdev pages and test ROMs, never from
the emulator's output.

## Region override

```cpp
void Machine::setRegionOverride(std::optional<Region> region); // nullopt = from header
std::optional<Region> Machine::regionOverride() const;
```

- The override takes effect at the next `loadCartridge` or `powerOn`. A
  running game is not retimed mid-frame.
- `region()` reports the effective region.
- Timing follows the nesdev "Cycle reference chart" and "Clock rate" pages,
  as the header-selected regions already do.
- **Test**: a NTSC-header ROM forced to PAL runs 312 scanlines per frame
  and 50.0070 fps (master-cycle counts per frame). The `pal_apu_tests`
  verdicts hold when they are forced.
- **API**: `core.set_region_override` (`region`: `auto`, `ntsc`, `pal`).
  `core.region` gains `override`.

## Palette

```cpp
// 64 entries: emphasis applied as the core does today; 512: entries 64*e + i
// for emphasis bits e = (PPUMASK >> 5) & 7. Empty: the built-in palette.
bool Machine::setPalette(std::span<const std::uint32_t> xrgb);   // false: bad size
```

- A `.pal` file is 192 bytes (64 RGB triplets) or 1536 bytes (512), as the
  nesdev "PPU palettes" page describes. The consumer parses the file and the
  core receives XRGB8888 values.
- The palette affects only the colour output, never emulation state, save
  states or frame hashes. Frame hashes must be palette-independent:
  G6 either hashes palette indices or hashes with the default palette.
- **Test**: with a 64-entry palette whose entry `$0F` is `0x123456`, a
  frame of colour `$0F` has every pixel `0x123456`. With 512 entries, the
  emphasis bits select the block.
- **API**: `core.set_palette` (`entries`: an array of 64 or 512 integers, or
  `bytes` holding a `.pal` file; empty restores the default).

## Game Genie

```cpp
struct GameGenieCode { std::uint16_t address; std::uint8_t value;
                       std::optional<std::uint8_t> compare; };
std::optional<GameGenieCode> decodeGameGenie(std::string_view code); // 6 or 8 letters
void Machine::setGameGenieCodes(std::span<const GameGenieCode> codes); // replaces the set
```

- **Decoding** follows the nesdev "Game Genie" page: the letter table
  `APZLGITYEOXUKSVN`, and the bit shuffle for 6- and 8-letter codes.
- **Substitution.**
  - A CPU read in `$8000-$FFFF` at `address` returns `value`. For an
    8-letter code, this happens only when the cartridge's byte equals
    `compare`.
  - Other reads are unaffected.
  - `peek` and the debugger see the substituted value, as the CPU does.
- **Limit.** The hardware accessory takes three codes. The core accepts any
  number, and the GUI does not impose the accessory's limit. The core
  contract states that this is a convenience beyond the hardware.
- Codes are not part of save states. They are session configuration, like
  the palette.
- **Tests**:
  - decoding the nesdev page's worked examples;
  - a test ROM reading a patched address with and without a compare match.
- **API**: `core.set_game_genie` (`codes`: an array of strings, with errors
  per invalid code) and `core.decode_game_genie` (`code`).

## As built (G060-G063)

- **Region.** `applyRegion` resolves the override, else the header, at
  `loadCartridge` and `powerOn`, rebuilding the timing-dependent components
  only when the region changes. The playtest harness's `region pal`
  directive now uses the override instead of rewriting the header, so the
  `pal_apu_tests` verdicts are the "holds when forced" test. In the GUI,
  Emulation > Region is the `emulation.region` setting, applied before
  every game loads; changing it during play says it takes effect at the
  next power cycle (`gui.set_region` returns `pending`).
- **Palette.** The PPU writes each pixel's palette index (`emphasis << 6 |
  colour`) beside the default-palette RGB. With a palette set, the machine
  colours the index buffer at frame delivery; `VideoFrame::referencePixels`
  (an appended field) keeps the default-palette frame. Frame hashes are
  taken from it, so every existing golden hash is unchanged, and a palette
  can never change a determinism or replay check. Why not hash indices:
  that would have changed every recorded hash for no gain. Built-in
  palettes: the default, and the RGB PPU (2C03) table from the nesdev
  "PPU palettes" page (`C = 255 * DAC / 7`). View > Palette also loads a
  `.pal` file; the path is stored, and a file that has gone falls back to
  the default.
- **Game Genie.** The bus substitutes reads at a code's address in
  `$8000-$FFFF`, in `read` and `peek`, so the CPU, DMA, `readMemory` and the
  debugger agree. Codes live in the bus, which survives a region rebuild,
  and are not serialized. The GUI's cheat list (FR-051) arrives in G7.

## Surface version

The new `core.*` commands are a MINOR bump of the API surface (002 R9), from
0.x to 0.(x+1). They are also added to the golden catalog. As built: 0.1 to
0.2.
