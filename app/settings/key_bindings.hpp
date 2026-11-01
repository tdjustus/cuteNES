#pragma once

// The Key Bindings page's model (contracts/menus-and-commands.md "Key
// Bindings page", data-model "Hotkey" and "InputBinding", FR-022): every
// menu action and game button with up to two bindings, the conflict rules,
// and the bindings that differ from the defaults, kept in the settings.
// Safe to use from any thread: the page reads it, and gui.* commands change
// it on the emulation thread.

#include "settings/app_settings.hpp"

#include <cutenes/qt/gamepad.hpp>

#include <QString>
#include <QVariantMap>
#include <QVector>

#include <array>
#include <mutex>
#include <optional>

namespace cutenes::app {

class KeyBindings {
public:
    using Pair = std::array<QString, 2>; // "" for none

    struct Definition {
        QString id;    // gui.quick_save, gui.save_state.3, p1.a
        QString label; // "File > Quick Save", "Player 1 A"
        bool control = false;
        Pair defaults;
    };
    struct Entry {
        Definition definition;
        Pair bindings;
    };
    struct Conflict {
        QString id; // "system" for a platform-reserved shortcut
        QString label;
    };

    explicit KeyBindings(AppSettings& settings);

    // Once, before any other call: the entries, with the stored bindings.
    void define(const QVector<Definition>& definitions);

    QVector<Entry> entries() const;
    std::optional<Entry> entry(const QString& id) const;

    // What else holds `binding` (normalized), or what the platform reserves.
    QVector<Conflict> conflicts(const QString& id, const QString& binding) const;

    struct Change {
        enum class Status { Done, UnknownId, BadBinding, BadIndex, Conflicted } status = Status::Done;
        QString why;
        QVector<Conflict> conflicts;
        std::optional<Entry> entry;
    };
    // Sets binding `index` of `id`. A conflict changes nothing unless
    // `force`, which takes the binding from the other entries.
    Change set(const QString& id, int index, const QString& binding, bool force);
    // index -1 clears both.
    Change clear(const QString& id, int index);
    // An empty id resets every entry. Returns the entries that changed, or
    // nothing for an unknown id.
    std::optional<QVector<Entry>> reset(const QString& id);

    // The stored bindings (those that differ from the defaults), by id.
    QMap<QString, Pair> stored(bool controls) const;

    // A menu action's id: its command, then each argument's value but the
    // hold actions' `on`, joined by ".".
    static QString idFor(const QString& command, const QVariantMap& args);
    // Portable text for a binding, or nothing when it is not one: a single
    // key combination for a hotkey, one key without modifiers for a game
    // button (Shift, Ctrl, Alt and Meta alone are their names). A gamepad
    // binding is "Pad:" and its controls (qt::padControlName), joined by
    // "+" in the enum's order: one for a game button, one or two for a
    // hotkey ("Pad:Back+LeftShoulder").
    static std::optional<QString> normalize(const QString& text, bool control, QString* why = nullptr);
    static bool isPad(const QString& binding) { return binding.startsWith(QLatin1String("Pad:")); }
    // A normalized pad binding's controls; empty for a key.
    static QVector<qt::PadControl> padControls(const QString& binding);
    // A game button's key (a Qt::Key) and back.
    static int keyOf(const QString& name);
    static QString keyName(int key);
    // How the page shows a binding: the platform's own text (⌘S on macOS).
    static QString displayText(const QString& binding, bool control);

private:
    int indexOf(const QString& id) const; // under m_mutex
    // Whether `binding` on both entries is a conflict: a pad control is
    // per player, so player 1's and player 2's may share it.
    static bool clashes(const Definition& a, const Definition& b, const QString& binding);
    void store(const Entry& e);           // under m_mutex

    AppSettings& m_settings;
    mutable std::mutex m_mutex;
    QVector<Entry> m_entries;
};

} // namespace cutenes::app
