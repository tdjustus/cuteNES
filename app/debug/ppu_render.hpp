#pragma once

// The PPU viewer's pictures (spec US9-4, FR-062), drawn from the PPU's
// memory as the hardware reads it (nesdev "PPU pattern tables", "PPU
// nametables", "PPU attribute tables", "PPU OAM", "PPU palettes"). Plain
// functions of the bytes, so they can be checked against the CHR data
// itself; the window only fetches the bytes and shows the images.

#include <QByteArray>
#include <QImage>

#include <array>
#include <cstdint>

namespace cutenes::app::ppu {

// The 64 output colours (XRGB8888) the viewer draws with.
using Colours = std::array<std::uint32_t, 64>;
// The four colours of one palette: 0-3 the background's, 4-7 the sprites'.
// Colour 0 of every palette is the backdrop, $3F00 (nesdev "PPU palettes").
using Four = std::array<std::uint32_t, 4>;

Four subPalette(const QByteArray& paletteRam, int palette, const Colours& colours);

// One pattern table ($0000 or $1000, 4096 bytes): 16 x 16 tiles, 128 x 128.
QImage patternTable(const QByteArray& chr, const Four& four);

// The four nametables ($2000-$2FFF as the PPU sees them, mirroring
// resolved), 512 x 480, drawn with the background pattern table (4096
// bytes) and palette RAM.
QImage nametables(const QByteArray& vram, const QByteArray& background, const QByteArray& paletteRam,
    const Colours& colours);

// The 64 sprites in OAM order, 8 to a row, each in an 8 x 16 cell (8 x 8
// sprites use the top half): 64 x 128. `chr` is all 8 KiB of pattern
// tables; `table` the 8 x 8 sprites' ($1000 or $0000, PPUCTRL bit 3).
// Colour 0 is transparent and shows the backdrop.
QImage sprites(const QByteArray& oam, const QByteArray& chr, bool tall, int table, const QByteArray& paletteRam,
    const Colours& colours);

} // namespace cutenes::app::ppu
