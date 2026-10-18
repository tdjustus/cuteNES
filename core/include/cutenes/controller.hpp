#pragma once

#include <bitset>
#include <cstdint>

namespace cutenes {

enum class Port : std::uint8_t { One = 1, Two = 2 };

// Serial-shift order — the exact bit order games clock out of $4016/$4017
// (data-model.md, ControllerState; spec FR-007).
enum class Button : std::uint8_t {
    Right, Left, Down, Up, Start, Select, B, A,
};

using ButtonSet = std::bitset<8>;

} // namespace cutenes
