#include "persistence/rewind_buffer.hpp"

#include <algorithm>

namespace cutenes::app {

namespace {

void putCount(std::vector<std::uint8_t>& out, std::size_t n)
{
    while (n >= 0x80) {
        out.push_back(static_cast<std::uint8_t>(n | 0x80));
        n >>= 7;
    }
    out.push_back(static_cast<std::uint8_t>(n));
}

std::size_t getCount(const std::uint8_t*& in)
{
    std::size_t n = 0;
    for (int shift = 0;; shift += 7) {
        const std::uint8_t b = *in++;
        n |= static_cast<std::size_t>(b & 0x7F) << shift;
        if (!(b & 0x80)) return n;
    }
}

// a XOR b as runs: [unchanged count][changed count][changed bytes XORed]…
std::vector<std::uint8_t> encodeXor(const std::vector<std::uint8_t>& a, const std::uint8_t* b)
{
    std::vector<std::uint8_t> out;
    const std::size_t size = a.size();
    std::size_t i = 0;
    while (i < size) {
        const std::size_t same = i;
        while (i < size && a[i] == b[i]) ++i;
        const std::size_t changed = i;
        while (i < size && a[i] != b[i]) ++i;
        putCount(out, changed - same);
        putCount(out, i - changed);
        for (std::size_t k = changed; k < i; ++k) out.push_back(static_cast<std::uint8_t>(a[k] ^ b[k]));
    }
    return out;
}

// Applies an encoded XOR to `target` in place (XOR is its own inverse).
void applyXor(std::vector<std::uint8_t>& target, const std::vector<std::uint8_t>& runs)
{
    const std::uint8_t* in = runs.data();
    const std::uint8_t* end = in + runs.size();
    std::size_t pos = 0;
    while (in < end) {
        pos += getCount(in);
        const std::size_t changed = getCount(in);
        for (std::size_t k = 0; k < changed; ++k) target[pos++] ^= *in++;
    }
}

} // namespace

void RewindBuffer::push(const std::uint8_t* state, std::size_t size, ButtonSet port1, ButtonSet port2)
{
    if (m_budget == 0) return;
    if (!m_entries.empty() && size != m_newest.size()) clear();
    Stored stored;
    stored.input[0] = port1;
    stored.input[1] = port2;
    if (m_entries.empty()) {
        stored.data.assign(state, state + size);
        stored.whole = true;
    } else {
        stored.data = encodeXor(m_newest, state);
    }
    m_bytes += stored.data.size();
    m_entries.push_back(std::move(stored));
    m_newest.assign(state, state + size);
    trim();
}

std::optional<RewindBuffer::Entry> RewindBuffer::pop()
{
    if (m_entries.empty()) return std::nullopt;
    Stored& top = m_entries.back();
    Entry entry{m_newest, {top.input[0], top.input[1]}};
    m_bytes -= top.data.size();
    if (top.whole) {
        m_newest.clear(); // it was the only one
    } else {
        applyXor(m_newest, top.data); // back to the state before it
    }
    m_entries.pop_back();
    return entry;
}

void RewindBuffer::clear()
{
    m_entries.clear();
    m_newest.clear();
    m_bytes = 0;
}

void RewindBuffer::setBudget(std::size_t bytes)
{
    m_budget = bytes;
    if (m_budget == 0) {
        clear();
        return;
    }
    trim();
}

void RewindBuffer::dropOldest()
{
    Stored oldest = std::move(m_entries.front());
    m_entries.pop_front();
    m_bytes -= oldest.data.size();
    if (m_entries.empty()) {
        m_newest.clear();
        return;
    }
    // The next one becomes whole: the oldest state with its XOR applied.
    Stored& next = m_entries.front();
    applyXor(oldest.data, next.data);
    m_bytes -= next.data.size();
    next.data = std::move(oldest.data);
    next.whole = true;
    m_bytes += next.data.size();
}

void RewindBuffer::trim()
{
    while (!m_entries.empty() && bytes() > m_budget) dropOldest();
}

} // namespace cutenes::app
