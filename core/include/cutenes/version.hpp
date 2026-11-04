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

// The emulation revision (#166): raised whenever the same input can give
// different output (frames, sound, timing), which the core version does not
// say because it also rises for new features. CI requires it to rise when a
// pinned golden or playtest hash changes (tools/check_emulation_revision.py);
// raise it too for a fix that changes behavior no pin covers. Movies record
// it and warn on playback when it differs.
inline constexpr std::uint32_t emulationRevision = 1;

// "0.2.0" — null-terminated, stable for the process lifetime.
const char* coreVersionString();

} // namespace cutenes
