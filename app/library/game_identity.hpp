#pragma once

// A game's identity and its per-game file names (research R7,
// contracts/library-layout.md "Game names").

#include <QByteArray>
#include <QString>

#include <optional>

namespace cutenes::app {

struct GameIdentity {
    QString sha1; // 40 lowercase hex digits

    QString id8() const { return sha1.left(8); }
    bool operator==(const GameIdentity&) const = default;
};

// The SHA-1 of an iNES / NES 2.0 image after its 16-byte header and any
// 512-byte trainer: the headerless hash No-Intro uses, so re-headered dumps
// share saves. Empty for anything that is not an iNES image.
std::optional<GameIdentity> identifyRom(const QByteArray& file);

// A file stem made safe for every file system: <>:"/\|?* and control
// characters become _, and the result is trimmed to 80 characters.
QString sanitizeDisplayName(const QString& stem);

// "<display name> [<id8>]", or "<display name> [<id8>-<n>]" for the n-th
// game (n >= 2) sharing that id8.
QString gameBaseName(const QString& displayName, const GameIdentity& id, int collision = 1);

} // namespace cutenes::app
