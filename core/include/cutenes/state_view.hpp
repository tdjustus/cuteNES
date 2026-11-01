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

// The PPU's scroll registers (nesdev "PPU scrolling"): t, the address the
// next frame starts from (yyy NN YYYYY XXXXX: fine Y, nametable, coarse Y,
// coarse X), v, the current address, the fine X scroll, and the $2005/$2006
// write toggle. The scroll in pixels counts across the four nametables.
struct PpuScroll {
    std::uint16_t t = 0;
    std::uint16_t v = 0;
    std::uint8_t fineX = 0;
    bool writeToggle = false;

    int scrollX() const { return (((t & 0x1F) << 3) | fineX) + ((t >> 10) & 1) * 256; }
    int scrollY() const { return ((((t >> 5) & 0x1F) << 3) | ((t >> 12) & 7)) + ((t >> 11) & 1) * 240; }
};

enum class WriteError : std::uint8_t {
    Ok,
    InvalidAddress,
};

// One instruction boundary with PPU position — nestest-format tracing and
// future MCP debug tooling. "PPU:%3u,%3u" is scanline,dot.
struct TracePoint {
    CpuState cpu;
    std::uint32_t scanline = 0;
    std::uint32_t dot = 0;
};

} // namespace cutenes
