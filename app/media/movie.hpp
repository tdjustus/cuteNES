#pragma once

// A movie (spec FR-052, data-model "Movie", library-layout "Movie file
// (.cnm)"): the controller input of every frame from a start point, which
// replays exactly because the core is deterministic.
//   CNM1\n
//   {"identity", "core", "region", "start": "power_on"|"state", "state",
//    "frames", "rerecords", "cheats", "battery",
//    "events": [{"frame", "event": "reset"|"power"|"cheats", "cheats"}]}\n
//   frames × 2 bytes: port 1's ButtonSet, then port 2's
// Since #121 a movie also carries what else changes the game: "cheats", the
// cheats switched on when recording started (cheats-file objects), with a
// "cheats" event wherever the set changed while recording, and "battery",
// the battery RAM (base64) a power-on movie started from (a state's start
// carries its own). Both are optional: a movie without them plays with
// whatever cheats and battery RAM the player has, as before.
// Plain data, used on the emulation thread.

#include "cheats/cheat_list.hpp"

#include <QByteArray>
#include <QString>
#include <QVector>

#include <cstdint>
#include <optional>

namespace cutenes::app {

struct Movie {
    enum class Start { PowerOn, State };
    // Before the frame `frame` ran: a button the console has, or (Cheats)
    // the cheats switched on from then.
    struct Event {
        enum class Kind { Reset, Power, Cheats };
        qint64 frame = 0;
        Kind kind = Kind::Reset;
        QVector<Cheat> cheats; // Kind::Cheats
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
    std::optional<QVector<Cheat>> cheats; // switched on at the start; nullopt: not recorded
    std::optional<QByteArray> battery;    // power-on: the battery RAM it started from

    qint64 frames() const { return input.size() / 2; }
    // Drops the frames from `frame` on, and their events.
    void truncate(qint64 frame);

    QByteArray serialize() const;
    static std::optional<Movie> parse(const QByteArray& bytes, QString* error = nullptr);
    bool write(const QString& path, QString* error = nullptr) const;
    static std::optional<Movie> read(const QString& path, QString* error = nullptr);
};

} // namespace cutenes::app
