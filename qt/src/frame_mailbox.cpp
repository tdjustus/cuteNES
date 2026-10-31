#include <cutenes/qt/frame_mailbox.hpp>

#include <algorithm>

namespace cutenes::qt {

void FrameMailbox::publish(const std::uint32_t* pixels, std::uint64_t frameIndex)
{
    std::lock_guard lock(m_mutex);
    std::copy(pixels, pixels + kPixels, m_pixels.begin());
    m_frameIndex = frameIndex;
    m_hasFrame = true;
    ++m_sequence;
}

bool FrameMailbox::takeNewer(std::vector<std::uint32_t>& out, std::uint64_t& seen,
    std::uint64_t& frameIndex) const
{
    std::lock_guard lock(m_mutex);
    if (m_sequence == seen) {
        return false;
    }
    if (m_hasFrame) {
        out = m_pixels;
    } else {
        out.clear();
    }
    frameIndex = m_hasFrame ? m_frameIndex : 0;
    seen = m_sequence;
    return true;
}

void FrameMailbox::clear()
{
    std::lock_guard lock(m_mutex);
    m_hasFrame = false;
    ++m_sequence; // readers repaint, now black
}

bool FrameMailbox::hasFrame() const
{
    std::lock_guard lock(m_mutex);
    return m_hasFrame;
}

} // namespace cutenes::qt
