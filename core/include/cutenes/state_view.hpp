#pragma once

#include <cstdint>

namespace cutenes {

// CPU observation snapshot (spec FR-016). P bit 5 always reads 1; the B flag
// does not exist in the register and is never materialized (research.md D4).
struct CpuState {
    std::uint8_t a = 0;
    std::uint8_t x = 0;
    std::uint8_t y = 0;
    std::uint8_t s = 0xFD;
    std::uint8_t p = 0x24;
    std::uint16_t pc = 0;
    std::uint64_t masterCycle = 0;
};

enum class WriteError : std::uint8_t {
    Ok,
    InvalidAddress,
};

} // namespace cutenes
