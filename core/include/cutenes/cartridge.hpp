#pragma once

#include <cstddef>
#include <cstdint>

namespace cutenes {

// Region resolved at load: NES 2.0 header flags when present, else NTSC
// default (data-model.md, Machine entity).
enum class Region : std::uint8_t { Ntsc, Pal, Unspecified };

enum class Mirroring : std::uint8_t {
    Horizontal,
    Vertical,
    FourScreen,
    MapperControlled,
};

// Every rejection names the exact defect (spec FR-001); `detail` carries the
// mapper id for UnsupportedMapper and the offending byte offset otherwise.
enum class LoadError : std::uint8_t {
    Ok,
    NotAnInesFile,
    TruncatedFile,
    InvalidHeader,
    InvalidBankCounts,
    UnsupportedMapper,
    Nes2FieldInvalid,
};

struct CartridgeInfo {
    std::uint8_t formatVersion = 0; // 0 = iNES 1.0, 2 = NES 2.0
    std::uint16_t mapperId = 0;     // 0,1,2,3,4 supported initially
    std::uint8_t submapper = 0;   // NES 2.0 byte 13 low nibble; 0 for iNES 1.0
    Mirroring mirroring = Mirroring::Horizontal;
    bool battery = false;
    std::size_t prgSize = 0;
    std::size_t chrSize = 0; // 0 => CHR-RAM
    std::size_t prgRamSize = 0;
    Region region = Region::Unspecified;
};

struct LoadResult {
    LoadError error = LoadError::Ok;
    std::uint16_t detail = 0;
    CartridgeInfo info{};
};

} // namespace cutenes
