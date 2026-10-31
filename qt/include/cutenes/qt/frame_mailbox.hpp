#pragma once

// The newest frame, handed from the emulation thread to the GUI thread.
// The writer never waits for the reader; a frame the view had no time to
// show is replaced by the next one.

#include <cstdint>
#include <mutex>
#include <vector>

namespace cutenes::qt {

class FrameMailbox {
public:
    static constexpr std::size_t kPixels = 256 * 240;

    // Any thread. Copies the 256x240 XRGB8888 frame.
    void publish(const std::uint32_t* pixels, std::uint64_t frameIndex);
    // When the mailbox changed since `seen`, copies the newest frame and its
    // index into `out` and `frameIndex` (or empties `out` if it was
    // cleared), updates `seen` and returns true.
    bool takeNewer(std::vector<std::uint32_t>& out, std::uint64_t& seen,
        std::uint64_t& frameIndex) const;
    // Back to "no frame" (the game closed).
    void clear();
    bool hasFrame() const;

private:
    mutable std::mutex m_mutex;
    std::vector<std::uint32_t> m_pixels = std::vector<std::uint32_t>(kPixels);
    std::uint64_t m_sequence = 0; // bumped by every publish and clear
    bool m_hasFrame = false;
    std::uint64_t m_frameIndex = 0;
};

} // namespace cutenes::qt
