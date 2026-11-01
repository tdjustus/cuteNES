#pragma once

// Game Genie codes (specs/003-qt-frontend/contracts/core-additions.md,
// nesdev "Game Genie"). The accessory sat between the console and the
// cartridge and replaced the byte of chosen CPU reads in $8000-$FFFF.

#include <cstdint>
#include <optional>
#include <string_view>

namespace cutenes {

struct GameGenieCode {
    std::uint16_t address = 0;
    std::uint8_t value = 0;
    // 8-letter codes: the substitution happens only when the cartridge's
    // byte equals this.
    std::optional<std::uint8_t> compare;

    bool operator==(const GameGenieCode&) const = default;
};

// A 6- or 8-letter code from the letters APZLGITYEOXUKSVN, in either case;
// nullopt for any other length or letter.
std::optional<GameGenieCode> decodeGameGenie(std::string_view code);

} // namespace cutenes
