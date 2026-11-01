// Game Genie decoding (nesdev "Game Genie"): each letter is a nibble, and
// the address, value and compare bits are shuffled across the nibbles.

#include <cutenes/game_genie.hpp>

#include <array>

namespace cutenes {

namespace {

constexpr std::string_view kLetters = "APZLGITYEOXUKSVN";

int nibble(char letter)
{
    const char upper = (letter >= 'a' && letter <= 'z') ? static_cast<char>(letter - 'a' + 'A') : letter;
    const std::size_t at = kLetters.find(upper);
    return at == std::string_view::npos ? -1 : static_cast<int>(at);
}

} // namespace

std::optional<GameGenieCode> decodeGameGenie(std::string_view code)
{
    if (code.size() != 6 && code.size() != 8) {
        return std::nullopt;
    }
    std::array<int, 8> n{};
    for (std::size_t i = 0; i < code.size(); ++i) {
        n[i] = nibble(code[i]);
        if (n[i] < 0) {
            return std::nullopt;
        }
    }
    GameGenieCode out;
    out.address = static_cast<std::uint16_t>(0x8000 + (((n[3] & 7) << 12) | ((n[5] & 7) << 8) | ((n[4] & 8) << 8)
                                                  | ((n[2] & 7) << 4) | ((n[1] & 8) << 4) | (n[4] & 7) | (n[3] & 8)));
    // The value's top-of-low-nibble bit comes from the last letter: n5 in a
    // 6-letter code, n7 in an 8-letter one; n5's then goes to the compare.
    const int last = code.size() == 6 ? n[5] : n[7];
    out.value = static_cast<std::uint8_t>(((n[1] & 7) << 4) | ((n[0] & 8) << 4) | (n[0] & 7) | (last & 8));
    if (code.size() == 8) {
        out.compare = static_cast<std::uint8_t>(((n[7] & 7) << 4) | ((n[6] & 8) << 4) | (n[6] & 7) | (n[5] & 8));
    }
    return out;
}

} // namespace cutenes
