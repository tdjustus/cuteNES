#pragma once

#include <cstdint>

namespace cutenes {

inline constexpr std::uint16_t coreVersionMajor = 0;
inline constexpr std::uint16_t coreVersionMinor = 2;
inline constexpr std::uint16_t coreVersionPatch = 0;

// Packed core release identifier stored in save-state headers
// (contracts/save-state-format.md): major << 22 | minor << 12 | patch.
// Raised with every release that changes the core (docs/source-control.md);
// states load across it, because the format version decides (#166).
inline constexpr std::uint64_t coreVersion =
    (std::uint64_t{coreVersionMajor} << 22) |
    (std::uint64_t{coreVersionMinor} << 12) |
    std::uint64_t{coreVersionPatch};

// "0.2.0" — null-terminated, stable for the process lifetime.
const char* coreVersionString();

} // namespace cutenes
