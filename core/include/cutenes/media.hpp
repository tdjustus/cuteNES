#pragma once

#include <cstddef>
#include <cstdint>

namespace cutenes {

// One complete 256x240 frame, XRGB8888 little-endian, row-major, stride 256,
// 61440 bytes total (contracts/media-output.md). `pixels` points at core-
// owned memory valid until the next frame is produced.
struct VideoFrame {
    const std::uint32_t* pixels = nullptr;
    std::uint64_t frameIndex = 0;
    // The same frame in the default palette: pixels itself unless a palette
    // is set (Machine::setPalette). Frame hashes use it, so they never
    // depend on the palette.
    const std::uint32_t* referencePixels = nullptr;
};

// Sinks are called synchronously during run*/step* calls. Keep work bounded:
// the core never blocks on a slow sink, and audio is delivered in chunks of
// at most 4096 samples.
struct IVideoSink {
    virtual void onFrame(const VideoFrame& frame) = 0;

protected:
    ~IVideoSink() = default;
};

struct IAudioSink {
    // pcm: mono signed 16-bit little-endian PCM at the configured output
    // rate; startMasterCycle ties the chunk to the master clock.
    virtual void onSamples(const std::int16_t* pcm, std::size_t count,
                           std::uint64_t startMasterCycle) = 0;

protected:
    ~IAudioSink() = default;
};

} // namespace cutenes
