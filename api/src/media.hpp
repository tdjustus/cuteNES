#pragma once

// Encoding helpers and the media captures the session keeps (tasks A025).

#include <cutenes/media.hpp>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <vector>

namespace cutenes::api::media {

inline constexpr std::size_t kFrameWidth = 256;
inline constexpr std::size_t kFrameHeight = 240;
inline constexpr std::size_t kFramePixels = kFrameWidth * kFrameHeight;

std::string base64Encode(const std::uint8_t* data, std::size_t size);
inline std::string base64Encode(const std::vector<std::uint8_t>& v)
{
    return base64Encode(v.data(), v.size());
}
// Standard alphabet with padding; nullopt on any malformed input.
std::optional<std::vector<std::uint8_t>> base64Decode(const std::string& text);

// FNV-1a 32-bit over the frame's 61440 XRGB8888 bytes, little-endian: the
// hash the playtest harness pins (tests/harness/main.cpp).
std::uint32_t fnv1a32(const std::uint8_t* data, std::size_t size);
std::string hashHex(std::uint32_t hash);

// RGB PNG of an XRGB8888 frame.
std::vector<std::uint8_t> encodePng(const std::uint32_t* pixels);

// Keeps the last delivered frame (IVideoSink).
class FrameCapture final : public IVideoSink {
public:
    void onFrame(const VideoFrame& frame) override;
    bool hasFrame() const { return m_hasFrame; }
    const std::vector<std::uint32_t>& pixels() const { return m_pixels; }
    std::uint64_t frameIndex() const { return m_frameIndex; }
    std::uint32_t hash() const;
    std::vector<std::uint8_t> bytes() const; // little-endian XRGB8888
    void clear();

private:
    std::vector<std::uint32_t> m_pixels = std::vector<std::uint32_t>(kFramePixels);
    std::uint64_t m_frameIndex = 0;
    bool m_hasFrame = false;
};

// The last ~10 s of audio, kept as the core delivers it (IAudioSink).
class AudioRing final : public IAudioSink {
public:
    explicit AudioRing(std::uint32_t sampleRate);
    void onSamples(const std::int16_t* pcm, std::size_t count,
        std::uint64_t startMasterCycle) override;

    struct Read {
        std::vector<std::int16_t> pcm;
        std::uint64_t startMasterCycle = 0; // of the first returned chunk
        std::uint64_t next = 0;      // sinceMasterCycle for the next call
        bool truncated = false;      // more samples are available
        bool dropped = false;        // some samples after `since` were evicted
    };
    // Whole chunks whose start is at or after `since`, up to maxSamples. A
    // chunk is cut only when it alone exceeds maxSamples.
    Read read(std::uint64_t since, std::size_t maxSamples) const;
    std::uint32_t sampleRate() const { return m_sampleRate; }
    void clear();

private:
    struct Chunk {
        std::uint64_t start;
        std::vector<std::int16_t> pcm;
    };
    std::uint32_t m_sampleRate;
    std::size_t m_capacity; // samples
    std::size_t m_size = 0;
    std::uint64_t m_evictedThrough = 0; // last start evicted, 0 = none
    bool m_evicted = false;
    std::deque<Chunk> m_chunks;
};

} // namespace cutenes::api::media
