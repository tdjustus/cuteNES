#pragma once

// Save-state slots (spec FR-033, US4, data-model "StateSlot", research R6):
// ten numbered slots per game in states/<game>/, each a core state blob
// (slot-NN.state) with a 256x240 thumbnail (slot-NN.png). The time shown is
// the state file's modification time. Every write is atomic (QSaveFile), so
// a crash never leaves half a state. Used on the emulation thread by the
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
        QDateTime savedAt;  // local time
        qint64 size = 0;    // bytes of the state
        QString statePath;  // the files, whether or not they exist
        QString thumbnailPath;
    };

    // No game: every slot is empty and nothing can be saved.
    StateSlots() = default;
    StateSlots(const Library& library, const GameIdentity& id, const QString& displayName);

    bool isOpen() const { return !m_dir.isEmpty(); }
    // states/<game>, created by the first save.
    QString folder() const { return m_dir; }

    static bool isValidSlot(int slot) { return slot >= 1 && slot <= kSlots; }

    Info info(int slot) const;
    // Writes the state and its thumbnail; false, with a reason, on failure.
    bool save(int slot, const QByteArray& state, const QImage& thumbnail, QString* error = nullptr);
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
