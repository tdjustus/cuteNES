#pragma once

// A game's cheats and watch list (spec FR-051/FR-053, data-model "Cheat",
// library-layout "cheats/<game>.json"):
//   {"identity": sha1,
//    "cheats": [{"label", "kind": "freeze"|"set"|"game_genie", "address",
//                "value", "code", "enabled"}],
//    "watches": [{"label", "address", "size"}]}
// freeze writes its value before every frame, set writes it once when
// switched on, and game_genie goes to the core. Plain data, used on the
// emulation thread.

#include "library/game_identity.hpp"

#include <QJsonObject>
#include <QString>
#include <QVector>

#include <cstdint>
#include <optional>

namespace cutenes::app {

struct Cheat {
    enum class Kind { Freeze, Set, GameGenie };
    QString label;
    Kind kind = Kind::Freeze;
    std::uint16_t address = 0; // freeze and set
    std::uint8_t value = 0;    // freeze and set
    QString code;              // game_genie, upper case
    bool enabled = true;
    bool operator==(const Cheat&) const = default;
};

struct Watch {
    QString label;
    std::uint16_t address = 0;
    int size = 1; // bytes, little-endian
    bool operator==(const Watch&) const = default;
};

QString kindName(Cheat::Kind kind);
std::optional<Cheat::Kind> kindFromName(const QString& name);

// One cheat as the cheats file stores it (and a movie, #121); nullopt for a
// kind this version does not know.
QJsonObject cheatToJson(const Cheat& cheat);
std::optional<Cheat> cheatFromJson(const QJsonObject& object);

struct CheatList {
    QVector<Cheat> cheats;
    QVector<Watch> watches;

    // A missing file is an empty list; an unreadable one sets `error`.
    static CheatList read(const QString& path, QString* error = nullptr);
    // Atomic (QSaveFile). An empty list removes the file.
    bool write(const QString& path, const GameIdentity& identity, QString* error = nullptr) const;
    bool empty() const { return cheats.isEmpty() && watches.isEmpty(); }
};

} // namespace cutenes::app
