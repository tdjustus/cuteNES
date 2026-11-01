#include "media.hpp"

#include "stb_image_write.h"

#include <algorithm>
#include <array>
#include <cstdio>

namespace cutenes::api::media {

namespace {

constexpr char kAlphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int decodeChar(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

void appendToVector(void* context, void* data, int size)
{
    auto* out = static_cast<std::vector<std::uint8_t>*>(context);
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    out->insert(out->end(), bytes, bytes + size);
}

} // namespace

std::string base64Encode(const std::uint8_t* data, std::size_t size)
{
    std::string out;
    out.reserve((size + 2) / 3 * 4);
    std::size_t i = 0;
    for (; i + 3 <= size; i += 3) {
        const std::uint32_t v = (std::uint32_t{data[i]} << 16)
            | (std::uint32_t{data[i + 1]} << 8) | data[i + 2];
        out += kAlphabet[(v >> 18) & 63];
        out += kAlphabet[(v >> 12) & 63];
        out += kAlphabet[(v >> 6) & 63];
        out += kAlphabet[v & 63];
    }
    if (size - i == 1) {
        const std::uint32_t v = std::uint32_t{data[i]} << 16;
        out += kAlphabet[(v >> 18) & 63];
        out += kAlphabet[(v >> 12) & 63];
        out += "==";
    } else if (size - i == 2) {
        const std::uint32_t v = (std::uint32_t{data[i]} << 16)
            | (std::uint32_t{data[i + 1]} << 8);
        out += kAlphabet[(v >> 18) & 63];
        out += kAlphabet[(v >> 12) & 63];
        out += kAlphabet[(v >> 6) & 63];
        out += '=';
    }
    return out;
}

std::optional<std::vector<std::uint8_t>> base64Decode(const std::string& text)
{
    if (text.size() % 4 != 0) {
        return std::nullopt;
    }
    std::vector<std::uint8_t> out;
    out.reserve(text.size() / 4 * 3);
    for (std::size_t i = 0; i < text.size(); i += 4) {
        const bool last = i + 4 == text.size();
        const int pad = last ? (text[i + 3] == '=') + (text[i + 2] == '=') : 0;
        if (pad == 1 && text[i + 2] == '=') {
            return std::nullopt; // "x=y=" style
        }
        std::array<int, 4> v{};
        for (int k = 0; k < 4 - pad; ++k) {
            v[static_cast<std::size_t>(k)] = decodeChar(text[i + static_cast<std::size_t>(k)]);
            if (v[static_cast<std::size_t>(k)] < 0) {
                return std::nullopt;
            }
        }
        const std::uint32_t n = (static_cast<std::uint32_t>(v[0]) << 18)
            | (static_cast<std::uint32_t>(v[1]) << 12)
            | (static_cast<std::uint32_t>(v[2]) << 6)
            | static_cast<std::uint32_t>(v[3]);
        out.push_back(static_cast<std::uint8_t>(n >> 16));
        if (pad < 2) out.push_back(static_cast<std::uint8_t>(n >> 8));
        if (pad < 1) out.push_back(static_cast<std::uint8_t>(n));
    }
    return out;
}

std::uint32_t fnv1a32(const std::uint8_t* data, std::size_t size)
{
    std::uint32_t hash = 0x811C9DC5u;
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= data[i];
        hash *= 0x01000193u;
    }
    return hash;
}

std::string hashHex(std::uint32_t hash)
{
    char text[9];
    std::snprintf(text, sizeof text, "%08x", hash);
    return text;
}

std::vector<std::uint8_t> encodePng(const std::uint32_t* pixels)
{
    std::vector<std::uint8_t> rgb(kFramePixels * 3);
    for (std::size_t i = 0; i < kFramePixels; ++i) {
        rgb[i * 3] = static_cast<std::uint8_t>(pixels[i] >> 16);
        rgb[i * 3 + 1] = static_cast<std::uint8_t>(pixels[i] >> 8);
        rgb[i * 3 + 2] = static_cast<std::uint8_t>(pixels[i]);
    }
    std::vector<std::uint8_t> png;
    stbi_write_png_to_func(appendToVector, &png, static_cast<int>(kFrameWidth),
        static_cast<int>(kFrameHeight), 3, rgb.data(),
        static_cast<int>(kFrameWidth * 3));
    return png;
}

void FrameCapture::onFrame(const VideoFrame& frame)
{
    std::copy(frame.pixels, frame.pixels + kFramePixels, m_pixels.begin());
    // The hash is of the default-palette frame (core-additions.md).
    const std::uint32_t* reference = frame.referencePixels ? frame.referencePixels : frame.pixels;
    std::vector<std::uint8_t> bytes(kFramePixels * 4);
    for (std::size_t i = 0; i < kFramePixels; ++i) {
        for (std::size_t b = 0; b < 4; ++b) bytes[i * 4 + b] = static_cast<std::uint8_t>(reference[i] >> (8 * b));
    }
    m_hash = fnv1a32(bytes.data(), bytes.size());
    m_frameIndex = frame.frameIndex;
    m_hasFrame = true;
}

std::vector<std::uint8_t> FrameCapture::bytes() const
{
    std::vector<std::uint8_t> out(kFramePixels * 4);
    for (std::size_t i = 0; i < kFramePixels; ++i) {
        const std::uint32_t p = m_pixels[i];
        out[i * 4] = static_cast<std::uint8_t>(p);
        out[i * 4 + 1] = static_cast<std::uint8_t>(p >> 8);
        out[i * 4 + 2] = static_cast<std::uint8_t>(p >> 16);
        out[i * 4 + 3] = static_cast<std::uint8_t>(p >> 24);
    }
    return out;
}

std::uint32_t FrameCapture::hash() const
{
    return m_hasFrame ? m_hash : fnv1a32(bytes().data(), kFramePixels * 4);
}

void FrameCapture::clear()
{
    std::fill(m_pixels.begin(), m_pixels.end(), 0u);
    m_frameIndex = 0;
    m_hasFrame = false;
    m_hash = 0;
}

AudioRing::AudioRing(std::uint32_t sampleRate)
    : m_sampleRate(sampleRate), m_capacity(std::size_t{sampleRate} * 10)
{
}

void AudioRing::onSamples(const std::int16_t* pcm, std::size_t count,
    std::uint64_t startMasterCycle)
{
    if (count == 0) {
        return;
    }
    m_chunks.push_back({startMasterCycle, std::vector<std::int16_t>(pcm, pcm + count)});
    m_size += count;
    while (m_size > m_capacity && m_chunks.size() > 1) {
        m_size -= m_chunks.front().pcm.size();
        m_evictedThrough = m_chunks.front().start;
        m_evicted = true;
        m_chunks.pop_front();
    }
}

AudioRing::Read AudioRing::read(std::uint64_t since, std::size_t maxSamples) const
{
    Read r;
    r.next = since;
    r.dropped = m_evicted && since <= m_evictedThrough;
    auto it = std::find_if(m_chunks.begin(), m_chunks.end(),
        [&](const Chunk& c) { return c.start >= since; });
    if (it == m_chunks.end()) {
        return r;
    }
    r.startMasterCycle = it->start;
    for (; it != m_chunks.end(); ++it) {
        const std::size_t room = maxSamples - r.pcm.size();
        if (it->pcm.size() > room) {
            if (r.pcm.empty()) {
                // A chunk larger than the request: return its head. The
                // rest stays reachable only by a larger request.
                r.pcm.assign(it->pcm.begin(),
                    it->pcm.begin() + static_cast<std::ptrdiff_t>(room));
                r.next = it->start + 1;
            } else {
                r.next = it->start;
            }
            r.truncated = true;
            return r;
        }
        r.pcm.insert(r.pcm.end(), it->pcm.begin(), it->pcm.end());
        r.next = it->start + 1;
    }
    return r;
}

void AudioRing::clear()
{
    m_chunks.clear();
    m_size = 0;
    m_evicted = false;
    m_evictedThrough = 0;
}

} // namespace cutenes::api::media
