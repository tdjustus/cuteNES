#pragma once

// Record Audio's file (spec FR-044): an uncompressed PCM WAV, 16-bit mono
// at the core's 48 kHz. The header's sizes are written on close, so a file
// cut short by a crash still holds the samples (with sizes of zero, which
// most players read to the end).

#include <QFile>
#include <QString>

#include <cstdint>

namespace cutenes::app {

class WavWriter {
public:
    explicit WavWriter(int sampleRate = 48000) : m_sampleRate(sampleRate) {}
    ~WavWriter() { close(); }
    WavWriter(const WavWriter&) = delete;
    WavWriter& operator=(const WavWriter&) = delete;

    bool open(const QString& path, QString* error = nullptr);
    void write(const std::int16_t* pcm, std::size_t count);
    // Writes the sizes and closes. Returns the samples written.
    std::uint64_t close();
    bool isOpen() const { return m_file.isOpen(); }
    QString path() const { return m_file.fileName(); }

private:
    void writeHeader(std::uint32_t dataBytes);

    int m_sampleRate;
    QFile m_file;
    std::uint64_t m_samples = 0;
};

} // namespace cutenes::app
