#include "settings/key_bindings.hpp"

#include <QKeySequence>
#include <QObject>

namespace cutenes::app {

namespace {

const QString kHotkeys = QStringLiteral("hotkeys");
const QString kInput = QStringLiteral("input");

struct ModifierKey {
    int key;
    const char* name;
};
// Qt names Cmd "Ctrl" and Control "Meta" on macOS, as in key sequences.
constexpr ModifierKey kModifierKeys[] = {
    {Qt::Key_Shift, "Shift"}, {Qt::Key_Control, "Ctrl"}, {Qt::Key_Alt, "Alt"}, {Qt::Key_Meta, "Meta"}};

bool isModifier(int key)
{
    for (const auto& m : kModifierKeys) {
        if (m.key == key) return true;
    }
    return key == Qt::Key_AltGr || key == Qt::Key_CapsLock;
}

// The platform's own shortcuts, which a binding would never reach.
QStringList reserved()
{
#ifdef Q_OS_MACOS
    return {QStringLiteral("Ctrl+Q"), QStringLiteral("Ctrl+H"), QStringLiteral("Ctrl+Tab")};
#else
    return {QStringLiteral("Alt+F4"), QStringLiteral("Alt+Tab")};
#endif
}

bool keepsOwn(const QString& id, const QString& binding)
{
    // Exit keeps the platform's quit shortcut.
#ifdef Q_OS_MACOS
    return id == QLatin1String("gui.exit") && binding == QLatin1String("Ctrl+Q");
#else
    return id == QLatin1String("gui.exit") && binding == QLatin1String("Alt+F4");
#endif
}

KeyBindings::Change failed(KeyBindings::Change::Status status, const QString& why)
{
    KeyBindings::Change change;
    change.status = status;
    change.why = why;
    return change;
}

QStringList toList(const KeyBindings::Pair& pair)
{
    return {pair[0], pair[1]};
}

} // namespace

KeyBindings::KeyBindings(AppSettings& settings) : m_settings(settings) {}

void KeyBindings::define(const QVector<Definition>& definitions)
{
    const QMap<QString, QStringList> hotkeys = m_settings.bindings(kHotkeys);
    const QMap<QString, QStringList> input = m_settings.bindings(kInput);
    std::lock_guard lock(m_mutex);
    m_entries.clear();
    for (const Definition& d : definitions) {
        Entry e{d, d.defaults};
        const auto& stored = d.control ? input : hotkeys;
        if (const auto it = stored.find(d.id); it != stored.end()) {
            for (std::size_t i = 0; i < 2; ++i) {
                const QString text = it.value().value(qsizetype(i));
                e.bindings[i] = text.isEmpty() ? QString() : normalize(text, d.control).value_or(QString());
            }
        }
        m_entries.push_back(e);
    }
}

int KeyBindings::indexOf(const QString& id) const
{
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries[i].definition.id == id) return i;
    }
    return -1;
}

QVector<KeyBindings::Entry> KeyBindings::entries() const
{
    std::lock_guard lock(m_mutex);
    return m_entries;
}

std::optional<KeyBindings::Entry> KeyBindings::entry(const QString& id) const
{
    std::lock_guard lock(m_mutex);
    const int i = indexOf(id);
    if (i < 0) return std::nullopt;
    return m_entries[i];
}

QVector<KeyBindings::Conflict> KeyBindings::conflicts(const QString& id, const QString& binding) const
{
    QVector<Conflict> out;
    if (binding.isEmpty()) return out;
    std::lock_guard lock(m_mutex);
    for (const Entry& e : m_entries) {
        if (e.definition.id == id) continue;
        if (e.bindings[0] == binding || e.bindings[1] == binding) out.push_back({e.definition.id, e.definition.label});
    }
    if (reserved().contains(binding) && !keepsOwn(id, binding)) {
        out.push_back({QStringLiteral("system"), QObject::tr("the system (%1)").arg(displayText(binding, false))});
    }
    return out;
}

void KeyBindings::store(const Entry& e)
{
    const bool isDefault = e.bindings == e.definition.defaults;
    m_settings.setBinding(e.definition.control ? kInput : kHotkeys, e.definition.id,
        isDefault ? QStringList() : toList(e.bindings));
}

KeyBindings::Change KeyBindings::set(const QString& id, int index, const QString& binding, bool force)
{
    Change change;
    std::optional<Entry> target = entry(id);
    if (!target) return failed(Change::Status::UnknownId, QStringLiteral("no binding has this id"));
    if (index < 0 || index > 1) return failed(Change::Status::BadIndex, QStringLiteral("must be 0 or 1"));
    QString why;
    const auto normalized = normalize(binding, target->definition.control, &why);
    if (!normalized) return failed(Change::Status::BadBinding, why);
    change.conflicts = conflicts(id, *normalized);
    if (!change.conflicts.isEmpty() && !force) {
        change.status = Change::Status::Conflicted;
        change.why = QStringLiteral("%1 is already used").arg(*normalized);
        return change;
    }
    std::lock_guard lock(m_mutex);
    for (Entry& e : m_entries) {
        bool changed = false;
        for (QString& b : e.bindings) {
            if (e.definition.id != id && b == *normalized) {
                b.clear();
                changed = true;
            }
        }
        if (changed) store(e);
    }
    Entry& e = m_entries[indexOf(id)];
    const auto slot = static_cast<std::size_t>(index);
    e.bindings[slot] = *normalized;
    if (e.bindings[1 - slot] == *normalized) e.bindings[1 - slot].clear();
    store(e);
    change.entry = e;
    return change;
}

KeyBindings::Change KeyBindings::clear(const QString& id, int index)
{
    std::lock_guard lock(m_mutex);
    const int i = indexOf(id);
    if (i < 0) return failed(Change::Status::UnknownId, QStringLiteral("no binding has this id"));
    if (index < -1 || index > 1) return failed(Change::Status::BadIndex, QStringLiteral("must be 0 or 1"));
    Entry& e = m_entries[i];
    for (std::size_t b = 0; b < 2; ++b) {
        if (index == -1 || index == int(b)) e.bindings[b].clear();
    }
    store(e);
    Change change;
    change.entry = e;
    return change;
}

std::optional<QVector<KeyBindings::Entry>> KeyBindings::reset(const QString& id)
{
    std::lock_guard lock(m_mutex);
    if (!id.isEmpty() && indexOf(id) < 0) return std::nullopt;
    QVector<Entry> changed;
    for (Entry& e : m_entries) {
        if (!id.isEmpty() && e.definition.id != id) continue;
        if (e.bindings == e.definition.defaults) continue;
        e.bindings = e.definition.defaults;
        store(e);
        changed.push_back(e);
    }
    // A default another entry took over is that entry's no longer.
    for (Entry& e : m_entries) {
        if (!id.isEmpty() && e.definition.id == id) continue;
        bool lost = false;
        for (QString& b : e.bindings) {
            for (const Entry& c : changed) {
                if (!b.isEmpty() && (c.bindings[0] == b || c.bindings[1] == b) && c.definition.id != e.definition.id) {
                    b.clear();
                    lost = true;
                }
            }
        }
        if (lost) {
            store(e);
            changed.push_back(e);
        }
    }
    return changed;
}

QMap<QString, KeyBindings::Pair> KeyBindings::stored(bool controls) const
{
    QMap<QString, Pair> out;
    std::lock_guard lock(m_mutex);
    for (const Entry& e : m_entries) {
        if (e.definition.control == controls && e.bindings != e.definition.defaults) {
            out.insert(e.definition.id, e.bindings);
        }
    }
    return out;
}

QString KeyBindings::idFor(const QString& command, const QVariantMap& args)
{
    QString id = command;
    for (auto it = args.begin(); it != args.end(); ++it) {
        if (it.key() == QLatin1String("on")) continue;
        id += QLatin1Char('.') + it.value().toString();
    }
    return id;
}

std::optional<QString> KeyBindings::normalize(const QString& text, bool control, QString* why)
{
    const auto fail = [why](const QString& reason) -> std::optional<QString> {
        if (why) *why = reason;
        return std::nullopt;
    };
    if (control) {
        const int key = keyOf(text);
        if (key == 0) return fail(QStringLiteral("a game button takes one key without modifiers"));
        return keyName(key);
    }
    const QKeySequence sequence = QKeySequence::fromString(text, QKeySequence::PortableText);
    if (sequence.count() != 1) return fail(QStringLiteral("must be one key combination, such as Ctrl+Shift+S"));
    const int key = sequence[0].key();
    if (key == 0 || key == Qt::Key_unknown || isModifier(key)) {
        return fail(QStringLiteral("must end with a key that is not a modifier, such as Ctrl+Shift+S"));
    }
    return sequence.toString(QKeySequence::PortableText);
}

int KeyBindings::keyOf(const QString& name)
{
    for (const auto& m : kModifierKeys) {
        if (name == QLatin1String(m.name)) return m.key;
    }
    const QKeySequence sequence = QKeySequence::fromString(name, QKeySequence::PortableText);
    if (sequence.count() != 1 || sequence[0].keyboardModifiers() != Qt::NoModifier) return 0;
    const int key = sequence[0].key();
    return key == Qt::Key_unknown ? 0 : key;
}

QString KeyBindings::keyName(int key)
{
    for (const auto& m : kModifierKeys) {
        if (m.key == key) return QLatin1String(m.name);
    }
    return QKeySequence(key).toString(QKeySequence::PortableText);
}

QString KeyBindings::displayText(const QString& binding, bool control)
{
    if (binding.isEmpty()) return {};
    if (control && isModifier(keyOf(binding))) {
        // A modifier alone has no native text of its own: take it from
        // "Shift+A" (⇧A on macOS, Shift+A elsewhere) less the A.
        QString text = QKeySequence::fromString(binding + QStringLiteral("+A"), QKeySequence::PortableText)
                           .toString(QKeySequence::NativeText);
        text.chop(1);
        if (text.endsWith(QLatin1Char('+'))) text.chop(1);
        return text;
    }
    return QKeySequence::fromString(binding, QKeySequence::PortableText).toString(QKeySequence::NativeText);
}

} // namespace cutenes::app
