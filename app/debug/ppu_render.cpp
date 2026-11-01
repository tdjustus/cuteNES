#include "debug/ppu_render.hpp"

namespace cutenes::app::ppu {

namespace {

std::uint8_t byteAt(const QByteArray& bytes, qsizetype i)
{
    return i >= 0 && i < bytes.size() ? static_cast<std::uint8_t>(bytes[i]) : 0;
}

// Pixel (x, y) of the tile at `offset`: the low plane is bytes 0-7, the
// high plane 8-15, bit 7 the leftmost pixel.
int tilePixel(const QByteArray& chr, qsizetype offset, int x, int y)
{
    const int lo = byteAt(chr, offset + y) >> (7 - x) & 1;
    const int hi = byteAt(chr, offset + 8 + y) >> (7 - x) & 1;
    return lo | hi << 1;
}

void drawTile(QImage& image, int left, int top, const QByteArray& chr, qsizetype offset, const Four& four,
    bool flipX = false, bool flipY = false)
{
    for (int y = 0; y < 8; ++y) {
        auto* line = reinterpret_cast<std::uint32_t*>(image.scanLine(top + y));
        for (int x = 0; x < 8; ++x) {
            const int value = tilePixel(chr, offset, flipX ? 7 - x : x, flipY ? 7 - y : y);
            line[left + x] = 0xFF000000u | four[static_cast<std::size_t>(value)];
        }
    }
}

} // namespace

Four subPalette(const QByteArray& paletteRam, int palette, const Colours& colours)
{
    Four four{};
    for (int i = 0; i < 4; ++i) {
        const int entry = i == 0 ? 0 : palette * 4 + i;
        four[static_cast<std::size_t>(i)] = colours[byteAt(paletteRam, entry) & 0x3F] & 0xFFFFFF;
    }
    return four;
}

QImage patternTable(const QByteArray& chr, const Four& four)
{
    QImage image(128, 128, QImage::Format_RGB32);
    for (int tile = 0; tile < 256; ++tile) drawTile(image, tile % 16 * 8, tile / 16 * 8, chr, tile * 16, four);
    return image;
}

QImage nametables(const QByteArray& vram, const QByteArray& background, const QByteArray& paletteRam,
    const Colours& colours)
{
    QImage image(512, 480, QImage::Format_RGB32);
    std::array<Four, 4> fours{};
    for (int p = 0; p < 4; ++p) fours[static_cast<std::size_t>(p)] = subPalette(paletteRam, p, colours);
    for (int table = 0; table < 4; ++table) {
        const qsizetype base = table * 0x400;
        for (int row = 0; row < 30; ++row) {
            for (int col = 0; col < 32; ++col) {
                const int tile = byteAt(vram, base + row * 32 + col);
                // One attribute byte covers 4 x 4 tiles, two bits per 2 x 2.
                const int attribute = byteAt(vram, base + 0x3C0 + row / 4 * 8 + col / 4);
                const int shift = (row % 4 / 2) * 4 + (col % 4 / 2) * 2;
                const int palette = attribute >> shift & 3;
                drawTile(image, table % 2 * 256 + col * 8, table / 2 * 240 + row * 8, background, tile * 16,
                    fours[static_cast<std::size_t>(palette)]);
            }
        }
    }
    return image;
}

QImage sprites(const QByteArray& oam, const QByteArray& chr, bool tall, int table, const QByteArray& paletteRam,
    const Colours& colours)
{
    QImage image(64, 128, QImage::Format_RGB32);
    image.fill(0xFF000000u | subPalette(paletteRam, 0, colours)[0]);
    for (int i = 0; i < 64; ++i) {
        const int tile = byteAt(oam, i * 4 + 1);
        const int attributes = byteAt(oam, i * 4 + 2);
        const Four four = subPalette(paletteRam, 4 + (attributes & 3), colours);
        const bool flipX = attributes & 0x40;
        const bool flipY = attributes & 0x80;
        const int left = i % 8 * 8;
        const int top = i / 8 * 16;
        // Colour 0 is transparent: drawn as the backdrop.
        Four shown = four;
        shown[0] = subPalette(paletteRam, 0, colours)[0];
        if (!tall) {
            drawTile(image, left, top, chr, (table ? 0x1000 : 0) + tile * 16, shown, flipX, flipY);
            continue;
        }
        // 8 x 16 (nesdev "PPU OAM" byte 1): bit 0 picks the table, the
        // top tile is even, and a vertical flip swaps the halves too.
        const qsizetype first = (tile & 1 ? 0x1000 : 0) + (tile & 0xFE) * 16;
        drawTile(image, left, top + (flipY ? 8 : 0), chr, first, shown, flipX, flipY);
        drawTile(image, left, top + (flipY ? 0 : 8), chr, first + 16, shown, flipX, flipY);
    }
    return image;
}

} // namespace cutenes::app::ppu
