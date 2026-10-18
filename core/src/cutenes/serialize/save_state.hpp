#pragma once

// Save-state codec (contracts/save-state-format.md; tasks T048/T049).
//
// Blob layout: 24-byte header ("CTNS", formatVersion, flags, coreVersion,
// region, reserved, cartridgeId, CRC-32 of the sections area) followed by
// tagged sections: tag(4) + length(4) + payload, all little-endian,
// fixed-width. Readers skip unknown tags (forward compatibility within a
// formatVersion). Loading validates completely before applying.

#include <cutenes/cutenes.hpp>

#include <cstddef>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <type_traits>
#include <vector>

namespace cutenes {

class StateWriter;

class StateReader {
public:
    explicit StateReader(const std::uint8_t* data, std::size_t size)
        : m_data(data)
        , m_size(size)
    {
    }

    // Position the cursor at a section's payload; false if absent or
    // truncated (validation use).
    bool seek(const char tag[4])
    {
        std::size_t off = kHeaderSize;
        while (off + 8 <= m_size) {
            const std::uint32_t len = readU32At(off + 4);
            if (std::memcmp(m_data + off, tag, 4) == 0) {
                if (off + 8 + len > m_size) {
                    return false;
                }
                m_cursor = off + 8;
                m_end = off + 8 + len;
                return true;
            }
            off += 8 + len;
        }
        return false;
    }

    template <typename T>
    T get()
    {
        static_assert(std::is_trivially_copyable_v<T>);
        T value{};
        std::memcpy(&value, m_data + m_cursor, sizeof(T));
        m_cursor += sizeof(T);
        return value;
    }

    void getBytes(std::uint8_t* dst, std::size_t n)
    {
        std::memcpy(dst, m_data + m_cursor, n);
        m_cursor += n;
    }

    // Header fields.
    std::uint16_t formatVersion() const
    {
        return readU16At(4);
    }
    std::uint16_t flags() const { return readU16At(6); }
    std::uint64_t coreVersion() const { return readU64At(8); }
    std::uint8_t region() const { return m_data[16]; }
    std::uint16_t cartridgeId() const { return readU16At(18); }
    std::uint32_t checksum() const { return readU32At(20); }

    std::size_t sectionsSize() const { return m_size - kHeaderSize; }

    static constexpr std::size_t kHeaderSize = 24;

private:
    std::uint16_t readU16At(std::size_t off) const
    {
        std::uint16_t v = 0;
        std::memcpy(&v, m_data + off, 2);
        return v;
    }
    std::uint32_t readU32At(std::size_t off) const
    {
        std::uint32_t v = 0;
        std::memcpy(&v, m_data + off, 4);
        return v;
    }
    std::uint64_t readU64At(std::size_t off) const
    {
        std::uint64_t v = 0;
        std::memcpy(&v, m_data + off, 8);
        return v;
    }

    const std::uint8_t* m_data;
    std::size_t m_size;
    std::size_t m_cursor = 0;
    std::size_t m_end = 0;
};

class StateWriter {
public:
    explicit StateWriter(std::vector<std::uint8_t>& out)
        : m_out(out)
    {
        m_out.resize(StateReader::kHeaderSize, 0);
    }

    // Section assembly: begin(tag), then w()/wBytes() any number of times.
    void begin(const char sectionTag[4])
    {
        for (int i = 0; i < 4; ++i) {
            m_out.push_back(static_cast<std::uint8_t>(sectionTag[i]));
        }
        m_sectionLen = m_out.size();
        m_out.resize(m_out.size() + 4, 0); // length patched in endSection
    }

    void end()
    {
        const std::uint32_t len = static_cast<std::uint32_t>(
            m_out.size() - m_sectionLen - 4);
        std::memcpy(m_out.data() + m_sectionLen, &len, 4);
    }

    template <typename T>
    void w(const T& value)
    {
        static_assert(std::is_trivially_copyable_v<T>);
        const auto* p = reinterpret_cast<const std::uint8_t*>(&value);
        m_out.insert(m_out.end(), p, p + sizeof(T));
    }

    void wBytes(const std::uint8_t* data, std::size_t n)
    {
        m_out.insert(m_out.end(), data, data + n);
    }

    // Fill the header; call once after all sections are written.
    void finish(std::uint16_t formatVersion, std::uint16_t flags,
        std::uint64_t version, std::uint8_t region,
        std::uint16_t cartridgeId)
    {
        m_out[0] = 'C';
        m_out[1] = 'T';
        m_out[2] = 'N';
        m_out[3] = 'S';
        putU16At(4, formatVersion);
        putU16At(6, flags);
        putU64At(8, version);
        m_out[16] = region;
        m_out[17] = 0;
        putU16At(18, cartridgeId);
        const std::uint32_t crc = crc32(m_out.data()
            + StateReader::kHeaderSize,
            m_out.size() - StateReader::kHeaderSize);
        putU32At(20, crc);
    }

    static std::uint32_t crc32(const std::uint8_t* data, std::size_t n)
    {
        std::uint32_t crc = 0xFFFFFFFFu;
        for (std::size_t i = 0; i < n; ++i) {
            crc ^= data[i];
            for (int b = 0; b < 8; ++b) {
                crc = (crc >> 1)
                    ^ (0xEDB88320u & (0u - (crc & 1u)));
            }
        }
        return ~crc;
    }

private:
    void putU16At(std::size_t off, std::uint16_t v)
    {
        std::memcpy(m_out.data() + off, &v, 2);
    }
    void putU32At(std::size_t off, std::uint32_t v)
    {
        std::memcpy(m_out.data() + off, &v, 4);
    }
    void putU64At(std::size_t off, std::uint64_t v)
    {
        std::memcpy(m_out.data() + off, &v, 8);
    }

    std::vector<std::uint8_t>& m_out;
    std::size_t m_sectionLen = 0;
};

} // namespace cutenes
