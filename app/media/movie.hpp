#pragma once

// A movie (spec FR-052, data-model "Movie", library-layout "Movie file
// (.cnm)"): the controller input of every frame from a start point, which
// replays exactly because the core is deterministic.
//   CNM1\n
//   {"identity", "core", "region", "start": "power_on"|"state", "state",
//    "frames", "rerecords", "events": [{"frame", "event": "reset"|"power"}]}\n
//   frames × 2 bytes: port 1's ButtonSet, then port 2's
// Plain data, used on the emulation thread.

#include <QByteArray>
#include <QString>
#include <QVector>

#include <cstdint>
#include <optional>

namespace cutenes::app {

struct Movie {
    enum class Start { PowerOn, State };
    // A button the console has, pressed before the frame `frame` ran.
    struct Event {
        enum class Kind { Reset, Power };
        qint64 frame = 0;
        Kind kind = Kind::Reset;
        bool operator==(const Event&) const = default;
    };

    QString identity; // the ROM's SHA-1
    QString core;     // the core version that recorded it
    QString region;   // "ntsc" or "pal"
    Start start = Start::PowerOn;
    QByteArray state; // Start::State: the core state it starts from
    QByteArray input; // two bytes a frame
    qint64 rerecords = 0;
    QVector<Event> events;

    qint64 frames() const { return input.size() / 2; }
    // Drops the frames from `frame` on, and their events.
    void truncate(qint64 frame);

    QByteArray serialize() const;
    static std::optional<Movie> parse(const QByteArray& bytes, QString* error = nullptr);
    bool write(const QString& path, QString* error = nullptr) const;
    static std::optional<Movie> read(const QString& path, QString* error = nullptr);
};

} // namespace cutenes::app
