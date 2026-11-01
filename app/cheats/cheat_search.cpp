#include "cheats/cheat_search.hpp"

namespace cutenes::app {

namespace {

constexpr std::uint16_t kPrgRamStart = 0x6000;

// Whether a value of `size` bytes at `address` lies wholly in one region.
bool fits(const CheatSearch::Snapshot& s, std::uint32_t address, int size)
{
    const std::uint32_t end = address + static_cast<std::uint32_t>(size);
    if (address < kPrgRamStart) return end <= s.ram.size();
    return end - kPrgRamStart <= s.prgRam.size();
}

} // namespace

std::uint32_t CheatSearch::valueAt(const Snapshot& snapshot, std::uint16_t address, int size)
{
    if (!fits(snapshot, address, size)) return 0;
    const bool prg = address >= kPrgRamStart;
    const std::vector<std::uint8_t>& bytes = prg ? snapshot.prgRam : snapshot.ram;
    const std::size_t at = prg ? address - kPrgRamStart : address;
    std::uint32_t value = bytes[at];
    if (size == 2) value |= static_cast<std::uint32_t>(bytes[at + 1]) << 8;
    return value;
}

void CheatSearch::reset(Snapshot snapshot, int size)
{
    m_size = size == 2 ? 2 : 1;
    m_candidates.clear();
    for (std::uint32_t a = 0; a < snapshot.ram.size(); ++a) {
        if (fits(snapshot, a, m_size)) m_candidates.push_back(static_cast<std::uint16_t>(a));
    }
    for (std::uint32_t i = 0; i < snapshot.prgRam.size(); ++i) {
        const std::uint32_t a = kPrgRamStart + i;
        if (fits(snapshot, a, m_size)) m_candidates.push_back(static_cast<std::uint16_t>(a));
    }
    m_previous = std::move(snapshot);
}

void CheatSearch::filter(Filter filter, std::optional<std::uint32_t> value, Snapshot now)
{
    std::vector<std::uint16_t> kept;
    for (const std::uint16_t address : m_candidates) {
        const std::uint32_t current = valueAt(now, address, m_size);
        const std::uint32_t before = valueAt(m_previous, address, m_size);
        const std::uint32_t other = value.value_or(before);
        bool pass = false;
        switch (filter) {
        case Filter::Equal: pass = current == other; break;
        case Filter::NotEqual: pass = current != other; break;
        case Filter::Greater: pass = current > other; break;
        case Filter::Less: pass = current < other; break;
        case Filter::Changed: pass = current != before; break;
        case Filter::Unchanged: pass = current == before; break;
        }
        if (pass && fits(now, address, m_size)) kept.push_back(address);
    }
    m_candidates = std::move(kept);
    m_previous = std::move(now);
}

std::vector<CheatSearch::Candidate> CheatSearch::results(const Snapshot& now, std::size_t max) const
{
    std::vector<Candidate> out;
    for (const std::uint16_t address : m_candidates) {
        if (out.size() >= max) break;
        out.push_back({address, valueAt(now, address, m_size), valueAt(m_previous, address, m_size)});
    }
    return out;
}

} // namespace cutenes::app
