#pragma once

// The rewind history (spec FR-034, research R6, data-model "RewindBuffer"):
// the machine's state at the start of each frame, with that frame's input,
// newest last. The oldest state is kept whole and every later one as the
// run-length-encoded XOR against the state before it, because most bytes do
// not change between frames. Popping walks back one XOR at a time and
// dropping the oldest folds one XOR into it, so both are cheap and nothing
// needs keyframes. Used on the emulation thread only.

#include <cutenes/controller.hpp>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <vector>

namespace cutenes::app {

class RewindBuffer {
public:
    static constexpr std::size_t kDefaultBudget = 64u << 20; // data-model "rewindBudgetMiB" 64

    struct Entry {
        std::vector<std::uint8_t> state;
        ButtonSet input[2];
    };

    explicit RewindBuffer(std::size_t budgetBytes = kDefaultBudget) : m_budget(budgetBytes) {}

    // The state at the start of a frame and the input that frame runs with.
    // A state of another size than the last (another game) starts over.
    void push(const std::uint8_t* state, std::size_t size, ButtonSet port1, ButtonSet port2);
    // The newest entry, removed; nothing when the history is empty.
    std::optional<Entry> pop();
    void clear();

    // A budget of 0 turns rewind off.
    void setBudget(std::size_t bytes);
    std::size_t budget() const { return m_budget; }
    bool empty() const { return m_entries.empty(); }
    std::size_t size() const { return m_entries.size(); }
    // Everything held, the newest state's decoded copy included.
    std::size_t bytes() const { return m_bytes + m_newest.size(); }

private:
    struct Stored {
        std::vector<std::uint8_t> data; // whole (the oldest) or an encoded XOR
        bool whole = false;
        ButtonSet input[2];
    };
    void dropOldest();
    void trim();

    std::size_t m_budget;
    std::deque<Stored> m_entries;
    std::vector<std::uint8_t> m_newest; // the newest state, decoded
    std::size_t m_bytes = 0;            // the sizes of m_entries' data
};

} // namespace cutenes::app
