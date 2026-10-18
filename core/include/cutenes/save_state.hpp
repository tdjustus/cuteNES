#pragma once

#include <cstddef>
#include <cstdint>

namespace cutenes {

// View of a serialized machine state. Valid until the next core call;
// consumers copy the bytes to keep them.
struct SaveStateBlob {
    const std::uint8_t* data = nullptr;
    std::size_t size = 0;
};

// Rejection is specific and never partial (contracts/save-state-format.md).
enum class LoadStateError : std::uint8_t {
    Ok,
    BadMagic,
    IncompatibleFormat,
    IncompatibleCoreVersion,
    WrongCartridge,
    ChecksumMismatch,
};

// Battery-backed cartridge RAM view. Size resolves from header/mapper
// (iNES flags-8 value 0 => 8 KiB compatibility default; research.md D10).
struct SramRange {
    const std::uint8_t* data = nullptr;
    std::size_t size = 0;
};

} // namespace cutenes
