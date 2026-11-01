#pragma once

// View > Palette (spec FR-041, core-additions.md "Palette"): the built-in
// palettes and .pal files, as the XRGB entries Machine::setPalette takes.

#include <QString>
#include <QStringList>

#include <cstdint>
#include <optional>
#include <vector>

namespace cutenes::app {

// "default" (the core's own, an empty list) and "rgb" (the RGB PPU, 2C03).
QStringList builtinPalettes();
// nullopt for an unknown name.
std::optional<std::vector<std::uint32_t>> builtinPalette(const QString& name);

// A .pal file: 64 (192 bytes) or 512 (1536 bytes) RGB triplets. On failure
// `error` says why.
std::optional<std::vector<std::uint32_t>> readPalFile(const QString& path, QString* error);

} // namespace cutenes::app
