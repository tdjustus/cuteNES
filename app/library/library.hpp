#pragma once

// The library folder (contracts/library-layout.md): the user's ROMs and
// everything cuteNES keeps per game. Per-game files are found by identity
// (R7): a name ends in "[<id8>]" or "[<id8>-<n>]", and the full identity is
// recorded beside it, in the folder's identities.json for flat files or in
// identity.txt inside a per-game folder.

#include "library/game_identity.hpp"

#include <QString>

namespace cutenes::app {

class Library {
public:
    enum class Folder { Roms, Saves, States, Screenshots, Movies, Recordings, Cheats, Debug };

    // <Documents>/cuteNES.
    static QString defaultRoot();

    explicit Library(QString root = defaultRoot());

    const QString& root() const { return m_root; }
    QString folder(Folder folder) const;

    // Creates the folder, its subfolders and roms/README.txt where missing.
    // False, with a reason, when the location cannot be created or written.
    bool ensureLayout(QString* error = nullptr) const;
    // The folder exists and is writable.
    bool isUsable() const;

    // The game's existing file (suffix ".sav", ".json", …) or per-game
    // folder (empty suffix) in `folder`; empty when it has none.
    QString findGameFile(Folder folder, const GameIdentity& id, const QString& suffix) const;
    // The existing one, or the path a new one should take, named after
    // `displayName` (sanitised).
    QString gameFile(Folder folder, const GameIdentity& id, const QString& displayName,
        const QString& suffix) const;
    // Moves the contents of this library into `target` (spec US2-4): each
    // subfolder is renamed when `target` has none, and otherwise (or across
    // volumes) its files are copied, keeping files `target` already has.
    // Nothing is deleted: a copied folder stays here for the user to remove.
    bool moveContentsTo(const Library& target, QString* error = nullptr) const;

    // Records which game `path` (a file or per-game folder in `folder`)
    // belongs to.
    void recordIdentity(Folder folder, const QString& path, const GameIdentity& id) const;

private:
    QString m_root;
};

} // namespace cutenes::app
