#pragma once

#include <cutenes/state_view.hpp>

#include <cstdint>
#include <functional>
#include <string>

namespace cutenes {

// Side-effect-free bus read for disassembly.
using Peek = std::uint8_t (*)(std::uint16_t);

// Build one nestest.log-format line for the given boundary. CYC counts CPU
// cycles of `masterPerCpuCycle` master ticks (the region's: 12 NTSC, 16
// PAL; #175).
std::string traceLine(const TracePoint& tp, Peek peek, std::uint64_t masterPerCpuCycle);

} // namespace cutenes
