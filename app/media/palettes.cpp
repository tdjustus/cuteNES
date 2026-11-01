#include "media/palettes.hpp"

#include <QFile>

#include <array>

namespace cutenes::app {

namespace {

// The RGB PPU's lookup ROM (nesdev "PPU palettes", 2C03/2C05): each digit
// is a 3-bit DAC level for red, green and blue.
constexpr std::array<int, 64> kRgbPpu = {
    333, 14, 6, 326, 403, 503, 510, 420, 320, 120, 31, 40, 22, 0, 0, 0,
    555, 36, 27, 407, 507, 704, 700, 630, 430, 140, 40, 53, 44, 0, 0, 0,
    777, 357, 447, 637, 707, 737, 740, 750, 660, 360, 70, 276, 77, 0, 0, 0,
    777, 567, 657, 757, 747, 755, 764, 772, 773, 572, 473, 276, 467, 0, 0, 0,
};

// C = 255 * DAC / 7 (the same page), rounded to the nearest level.
std::uint32_t level(int dac)
{
    return static_cast<std::uint32_t>((255 * dac + 3) / 7);
}

} // namespace

QStringList builtinPalettes()
{
    return {QStringLiteral("default"), QStringLiteral("rgb")};
}

std::optional<std::vector<std::uint32_t>> builtinPalette(const QString& name)
{
    if (name == QLatin1String("default")) return std::vector<std::uint32_t>{};
    if (name != QLatin1String("rgb")) return std::nullopt;
    std::vector<std::uint32_t> out;
    out.reserve(kRgbPpu.size());
    for (const int digits : kRgbPpu) {
        out.push_back(level(digits / 100) << 16 | level(digits / 10 % 10) << 8 | level(digits % 10));
    }
    return out;
}

std::optional<std::vector<std::uint32_t>> readPalFile(const QString& path, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("cannot read %1").arg(path);
        return std::nullopt;
    }
    const QByteArray bytes = file.read(1537);
    if (bytes.size() != 192 && bytes.size() != 1536) {
        if (error) *error = QStringLiteral("a .pal file is 192 or 1536 bytes; %1 is %2").arg(path).arg(bytes.size());
        return std::nullopt;
    }
    std::vector<std::uint32_t> out;
    for (qsizetype i = 0; i < bytes.size(); i += 3) {
        const auto byte = [&](qsizetype at) { return static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[at])); };
        out.push_back(byte(i) << 16 | byte(i + 1) << 8 | byte(i + 2));
    }
    return out;
}

} // namespace cutenes::app
