#include "media/wav_writer.hpp"

#include <QtEndian>

#include <algorithm>
#include <limits>

namespace cutenes::app {

namespace {

void put32(char* at, std::uint32_t value)
{
    qToLittleEndian<quint32>(value, at);
}

void put16(char* at, std::uint16_t value)
{
    qToLittleEndian<quint16>(value, at);
}

} // namespace

bool WavWriter::open(const QString& path, QString* error)
{
    close();
    m_file.setFileName(path);
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) *error = m_file.errorString();
        return false;
    }
    m_samples = 0;
    writeHeader(0);
    return true;
}

void WavWriter::writeHeader(std::uint32_t dataBytes)
{
    // The canonical 44-byte header: RIFF, a 16-byte PCM fmt chunk, data.
    char h[44];
    std::copy_n("RIFF", 4, h);
    put32(h + 4, 36 + dataBytes);
    std::copy_n("WAVEfmt ", 8, h + 8);
    put32(h + 16, 16);
    put16(h + 20, 1); // PCM
    put16(h + 22, 1); // mono
    put32(h + 24, std::uint32_t(m_sampleRate));
    put32(h + 28, std::uint32_t(m_sampleRate) * 2);
    put16(h + 32, 2);
    put16(h + 34, 16);
    std::copy_n("data", 4, h + 36);
    put32(h + 40, dataBytes);
    m_file.write(h, sizeof h);
}

void WavWriter::write(const std::int16_t* pcm, std::size_t count)
{
    if (!m_file.isOpen() || count == 0) return;
    // A WAV holds at most 4 GiB of data.
    const std::uint64_t room = (std::numeric_limits<std::uint32_t>::max() - 36) / 2 - m_samples;
    count = std::size_t(std::min<std::uint64_t>(count, room));
    QByteArray bytes(qsizetype(count * 2), Qt::Uninitialized);
    qToLittleEndian<qint16>(pcm, qsizetype(count), bytes.data());
    m_file.write(bytes);
    m_samples += count;
}

std::uint64_t WavWriter::close()
{
    if (!m_file.isOpen()) return 0;
    m_file.seek(0);
    writeHeader(std::uint32_t(m_samples * 2));
    m_file.close();
    return m_samples;
}

} // namespace cutenes::app
