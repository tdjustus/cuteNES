#pragma once

// The cheat search (spec FR-050, US8-1/2, data-model "CheatSearch"): a
// snapshot of CPU RAM ($0000-$07FF) and PRG-RAM ($6000-$7FFF, when the
// cartridge has it), and the addresses still in the running. Each filter
// compares the new snapshot with the previous one, or with a value, and
// keeps the addresses that pass. Values are 1 or 2 bytes, unsigned,
// little-endian; a 2-byte value never spans the gap between RAM and PRG-RAM.
// Plain data: the gui.cheat_search_* commands take the snapshots at frame
// boundaries on the emulation thread.

#include <cstdint>
#include <optional>
#include <vector>

namespace cutenes::app {

class CheatSearch {
public:
    enum class Filter { Equal, NotEqual, Greater, Less, Changed, Unchanged };

    // The memory a snapshot covers: RAM, then `prgRam` bytes of PRG-RAM.
    struct Snapshot {
        std::vector<std::uint8_t> ram;    // $0000-$07FF
        std::vector<std::uint8_t> prgRam; // $6000-, empty without PRG-RAM
    };

    struct Candidate {
        std::uint16_t address = 0;
        std::uint32_t value = 0;
        std::uint32_t previous = 0; // at the last filter (or reset)
    };

    // Every address again, with values of `size` bytes (1 or 2).
    void reset(Snapshot snapshot, int size);
    bool active() const { return m_size != 0; }
    int size() const { return m_size; }
    std::size_t count() const { return m_candidates.size(); }

    // Compares `now` with the previous snapshot, or with `value` (needed for
    // nothing but Changed and Unchanged, which ignore it). `now` becomes the
    // previous snapshot.
    void filter(Filter filter, std::optional<std::uint32_t> value, Snapshot now);

    // Up to `max` candidates in address order, with their values in `now`.
    std::vector<Candidate> results(const Snapshot& now, std::size_t max) const;

    // The value at `address` in `snapshot` (0 outside it).
    static std::uint32_t valueAt(const Snapshot& snapshot, std::uint16_t address, int size);

private:
    std::vector<std::uint16_t> m_candidates;
    Snapshot m_previous;
    int m_size = 0;
};

} // namespace cutenes::app
