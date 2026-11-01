#pragma once

// Save-state slots (spec FR-033, US4, data-model "StateSlot", research R6):
// ten numbered slots per game in states/<game>/, each a core state blob
// (slot-NN.state) with a 256x240 thumbnail (slot-NN.png) and the name the
// user gave it (slot-NN.json). The time shown is the state file's
// modification time. The folder belongs to the game's identity, the SHA-1
// of the ROM (R7), so renaming or moving the ROM keeps its states. Every
// write is atomic (QSaveFile), so a crash never leaves half a state. Used
// on the emulation thread by the
// gui.* commands; the menus read slot info on the GUI thread, which only
// looks at the files.

#include "library/library.hpp"

#include <QByteArray>
#include <QDateTime>
#include <QImage>
#include <QString>

#include <optional>

namespace cutenes::app {

class StateSlots {
public:
    static constexpr int kSlots = 10;

    struct Info {
        int slot = 0;
        bool empty = true;
        QString name;       // the user's, or the time it was saved
        QDateTime savedAt;  // local time
        qint64 size = 0;    // bytes of the state
        QString statePath;  // the files, whether or not they exist
        QString thumbnailPath;
        QString metadataPath;
    };

    // No game: every slot is empty and nothing can be saved.
    StateSlots() = default;
    StateSlots(const Library& library, const GameIdentity& id, const QString& displayName);

    bool isOpen() const { return !m_dir.isEmpty(); }
    // states/<game>, created by the first save.
    QString folder() const { return m_dir; }

    static bool isValidSlot(int slot) { return slot >= 1 && slot <= kSlots; }

    Info info(int slot) const;
    // Writes the state, its thumbnail and its name; false, with a reason,
    // on failure. An empty name becomes defaultName().
    bool save(int slot, const QByteArray& state, const QImage& thumbnail, const QString& name = {},
        QString* error = nullptr);
    // The name a new state gets: when it was saved, in the user's locale.
    static QString defaultName(const QDateTime& when = QDateTime::currentDateTime());
    // The slot's state; nothing when it is empty or cannot be read.
    std::optional<QByteArray> load(int slot, QString* error = nullptr) const;

    // A state file anywhere (Save State to File… / Load State from File…).
    static bool writeFile(const QString& path, const QByteArray& state, QString* error = nullptr);
    static std::optional<QByteArray> readFile(const QString& path, QString* error = nullptr);

private:
    QString m_dir;
    GameIdentity m_id;
    Library m_library;
};

} // namespace cutenes::app
