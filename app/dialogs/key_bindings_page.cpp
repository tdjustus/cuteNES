#include "dialogs/key_bindings_page.hpp"

#include <QHBoxLayout>
#include <QHash>
#include <QHeaderView>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace cutenes::app {

namespace {

constexpr int kIdRole = Qt::UserRole;
constexpr int kControlRole = Qt::UserRole + 1;

bool isModifierKey(int key)
{
    return key == Qt::Key_Shift || key == Qt::Key_Control || key == Qt::Key_Alt || key == Qt::Key_Meta
        || key == Qt::Key_AltGr || key == Qt::Key_CapsLock;
}

// "File > Save State > Slot 3": the group (File) and the row (Save State >
// Slot 3). Game buttons group by player.
std::pair<QString, QString> split(const KeyBindings::Definition& d)
{
    if (d.control) {
        const QString player = d.id.startsWith(QLatin1String("p1.")) ? QObject::tr("Player 1") : QObject::tr("Player 2");
        return {QObject::tr("Game Controls: %1").arg(player), d.label.section(QLatin1Char(' '), 2)}; // "Player 1 A"
    }
    const qsizetype cut = d.label.indexOf(QLatin1String(" > "));
    if (cut < 0) return {d.label, d.label};
    return {d.label.left(cut), d.label.mid(cut + 3)};
}

} // namespace

KeyBindingsPage::KeyBindingsPage(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("keyBindingsPage"));
    auto* title = new QLabel(tr("Key Bindings"), this);
    QFont big = title->font();
    big.setPointSizeF(big.pointSizeF() * 1.45);
    big.setWeight(QFont::DemiBold);
    title->setFont(big);
    auto* hint = new QLabel(tr("Click a binding, or select it and press Enter, then press the new key or "
                               "gamepad button. Esc cancels. Delete clears the selected binding."),
        this);
    hint->setWordWrap(true);
    m_filter = new QLineEdit(this);
    m_filter->setObjectName(QStringLiteral("keyBindingsFilter"));
    m_filter->setPlaceholderText(tr("Filter actions"));
    m_filter->setAccessibleName(tr("Filter actions"));
    m_filter->setClearButtonEnabled(true);
    m_filter->setMaximumWidth(260);
    connect(m_filter, &QLineEdit::textChanged, this, &KeyBindingsPage::applyFilter);

    m_tree = new QTreeWidget(this);
    m_tree->setObjectName(QStringLiteral("keyBindingsTree"));
    m_tree->setAccessibleName(tr("Key bindings"));
    m_tree->setColumnCount(3);
    m_tree->setHeaderLabels({tr("Action"), tr("Binding"), tr("Second Binding")});
    m_tree->setSelectionBehavior(QAbstractItemView::SelectItems);
    m_tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tree->setAllColumnsShowFocus(false);
    m_tree->setUniformRowHeights(true);
    m_tree->setAlternatingRowColors(true);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setStretchLastSection(false);
    m_tree->header()->resizeSection(1, 150);
    m_tree->header()->resizeSection(2, 150);
    m_tree->installEventFilter(this);
    connect(m_tree, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* item, int column) {
        if (column >= 1) startCapture(item, column);
    });
    connect(m_tree, &QTreeWidget::currentItemChanged, this, [this] { updateButtons(); });

    auto* back = new QPushButton(tr("Back"), this);
    back->setObjectName(QStringLiteral("keyBindingsBack"));
    m_clear = new QPushButton(tr("Clear"), this);
    m_clear->setObjectName(QStringLiteral("keyBindingsClear"));
    m_reset = new QPushButton(tr("Reset"), this);
    m_reset->setObjectName(QStringLiteral("keyBindingsReset"));
    m_reset->setToolTip(tr("Reset the selected action to its default keys"));
    auto* resetAll = new QPushButton(tr("Reset All"), this);
    resetAll->setObjectName(QStringLiteral("keyBindingsResetAll"));
    resetAll->setToolTip(tr("Reset every binding to its default"));
    for (QPushButton* b : {back, m_clear, m_reset, resetAll}) b->setAutoDefault(false);
    connect(back, &QPushButton::clicked, this, &KeyBindingsPage::backRequested);
    connect(m_clear, &QPushButton::clicked, this, [this] {
        if (!currentId().isEmpty()) emit clearRequested(currentId(), qMax(0, m_tree->currentColumn() - 1));
    });
    connect(m_reset, &QPushButton::clicked, this, [this] {
        if (!currentId().isEmpty()) emit resetRequested(currentId());
    });
    connect(resetAll, &QPushButton::clicked, this, [this] { emit resetRequested({}); });

    auto* buttons = new QHBoxLayout;
    buttons->addWidget(back);
    buttons->addStretch();
    buttons->addWidget(m_clear);
    buttons->addWidget(m_reset);
    buttons->addWidget(resetAll);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 16);
    layout->setSpacing(10);
    auto* top = new QHBoxLayout;
    top->addWidget(title);
    top->addStretch();
    top->addWidget(m_filter);
    layout->addLayout(top);
    layout->addWidget(hint);
    layout->addWidget(m_tree, 1);
    layout->addLayout(buttons);
    updateButtons();
}

void KeyBindingsPage::setEntries(const QVector<KeyBindings::Entry>& entries)
{
    if (m_capture) endCapture();
    // The same entries with new bindings: the rows stay, and so do the
    // selection and the scroll position.
    bool same = entries.size() == m_entries.size();
    for (qsizetype i = 0; same && i < entries.size(); ++i) same = entries[i].definition.id == m_entries[i].definition.id;
    if (same && m_tree->topLevelItemCount() > 0) {
        m_entries = entries;
        QHash<QString, const KeyBindings::Entry*> byId;
        for (const KeyBindings::Entry& e : m_entries) byId.insert(e.definition.id, &e);
        for (QTreeWidgetItemIterator it(m_tree); *it; ++it) {
            const KeyBindings::Entry* e = byId.value((*it)->data(0, kIdRole).toString());
            if (e) setBindingTexts(*it, *e);
        }
        updateButtons();
        return;
    }
    const QString current = currentId();
    const int column = qMax(1, m_tree->currentColumn());
    m_entries = entries;
    m_tree->clear();
    QMap<QString, QTreeWidgetItem*> groups;
    QTreeWidgetItem* select = nullptr;
    for (const KeyBindings::Entry& e : entries) {
        const auto [group, name] = split(e.definition);
        QTreeWidgetItem*& parent = groups[group];
        if (!parent) {
            parent = new QTreeWidgetItem(m_tree, {group});
            parent->setFlags(Qt::ItemIsEnabled);
            QFont bold = parent->font(0);
            bold.setWeight(QFont::DemiBold);
            parent->setFont(0, bold);
            parent->setFirstColumnSpanned(true);
        }
        auto* item = new QTreeWidgetItem(parent, {name});
        item->setData(0, kIdRole, e.definition.id);
        item->setData(0, kControlRole, e.definition.control);
        item->setToolTip(0, e.definition.label);
        setBindingTexts(item, e);
        if (e.definition.id == current) select = item;
    }
    m_tree->expandAll();
    if (select) m_tree->setCurrentItem(select, column);
    applyFilter(m_filter->text());
    updateButtons();
}

void KeyBindingsPage::applyFilter(const QString& text)
{
    // Rows whose action or binding contains the text; a group with none hides.
    for (int g = 0; g < m_tree->topLevelItemCount(); ++g) {
        QTreeWidgetItem* group = m_tree->topLevelItem(g);
        const bool groupMatches = group->text(0).contains(text, Qt::CaseInsensitive);
        int shown = 0;
        for (int r = 0; r < group->childCount(); ++r) {
            QTreeWidgetItem* row = group->child(r);
            const bool match = text.isEmpty() || groupMatches || row->text(0).contains(text, Qt::CaseInsensitive)
                || row->text(1).contains(text, Qt::CaseInsensitive) || row->text(2).contains(text, Qt::CaseInsensitive);
            row->setHidden(!match);
            shown += match ? 1 : 0;
        }
        group->setHidden(shown == 0);
    }
}

void KeyBindingsPage::setBindingTexts(QTreeWidgetItem* item, const KeyBindings::Entry& e)
{
    for (int c = 1; c <= 2; ++c) {
        const QString text = KeyBindings::displayText(e.bindings[std::size_t(c - 1)], e.definition.control);
        item->setText(c, text);
        item->setData(c, Qt::AccessibleTextRole,
            tr("%1, binding %2: %3").arg(e.definition.label).arg(c).arg(text.isEmpty() ? tr("none") : text));
    }
}

void KeyBindingsPage::showSection(const QString& section)
{
    const bool controls = section == QLatin1String("controls");
    for (QTreeWidgetItemIterator it(m_tree); *it; ++it) {
        if ((*it)->data(0, kIdRole).toString().isEmpty()) continue;
        if ((*it)->data(0, kControlRole).toBool() == controls) {
            m_tree->setCurrentItem(*it, 1);
            m_tree->scrollToItem((*it)->parent() ? (*it)->parent() : *it, QAbstractItemView::PositionAtTop);
            break;
        }
    }
    m_tree->setFocus();
}

QString KeyBindingsPage::currentId() const
{
    const QTreeWidgetItem* item = m_tree->currentItem();
    return item ? item->data(0, kIdRole).toString() : QString();
}

void KeyBindingsPage::updateButtons()
{
    const bool row = !currentId().isEmpty();
    m_clear->setEnabled(row);
    m_reset->setEnabled(row);
}

void KeyBindingsPage::startCapture(QTreeWidgetItem* item, int column)
{
    if (!item || item->data(0, kIdRole).toString().isEmpty() || column < 1) return;
    if (m_capture) endCapture();
    m_capture = item;
    m_captureColumn = column;
    m_tree->setCurrentItem(item, column);
    item->setText(column, tr("Press a key or button… (Esc cancels)"));
    m_padCapture.clear();
    m_tree->setFocus();
}

void KeyBindingsPage::endCapture()
{
    if (!m_capture) return;
    const QString id = m_capture->data(0, kIdRole).toString();
    for (const KeyBindings::Entry& e : m_entries) {
        if (e.definition.id == id) {
            m_capture->setText(m_captureColumn,
                KeyBindings::displayText(e.bindings[std::size_t(m_captureColumn - 1)], e.definition.control));
        }
    }
    m_capture = nullptr;
}

void KeyBindingsPage::padControl(qt::PadControl control, bool pressed)
{
    if (!m_capture) return;
    const bool game = m_capture->data(0, kControlRole).toBool();
    QString binding;
    if (pressed) {
        if (!m_padCapture.contains(control)) m_padCapture.push_back(control);
        if (!game) return; // a combination ends at the first release
        binding = QStringLiteral("Pad:") + QLatin1String(qt::padControlName(control));
    } else {
        if (!m_padCapture.contains(control)) return; // held from before
        QStringList names;
        for (const qt::PadControl c : m_padCapture) names << QLatin1String(qt::padControlName(c));
        binding = QStringLiteral("Pad:") + names.join(QLatin1Char('+'));
    }
    const QString id = m_capture->data(0, kIdRole).toString();
    const int index = m_captureColumn - 1;
    endCapture();
    emit bindRequested(id, index, binding);
}

bool KeyBindingsPage::eventFilter(QObject* watched, QEvent* event)
{
    if (watched != m_tree) return QWidget::eventFilter(watched, event);
    if (event->type() == QEvent::ShortcutOverride && m_capture) {
        event->accept(); // the key is the binding, not a menu shortcut
        return true;
    }
    if (event->type() != QEvent::KeyPress) return QWidget::eventFilter(watched, event);
    auto* key = static_cast<QKeyEvent*>(event);
    if (m_capture) {
        if (key->isAutoRepeat()) return true;
        if (key->key() == Qt::Key_Escape) {
            endCapture();
            return true;
        }
        const bool control = m_capture->data(0, kControlRole).toBool();
        QString binding;
        if (control) {
            binding = KeyBindings::keyName(key->key());
        } else if (isModifierKey(key->key())) {
            return true; // a combination ends with its key
        } else {
            const Qt::KeyboardModifiers mods = key->modifiers() & ~Qt::KeypadModifier;
            binding = QKeySequence(QKeyCombination(mods, Qt::Key(key->key()))).toString(QKeySequence::PortableText);
        }
        const QString id = m_capture->data(0, kIdRole).toString();
        const int index = m_captureColumn - 1;
        endCapture();
        emit bindRequested(id, index, binding);
        return true;
    }
    switch (key->key()) {
    case Qt::Key_Return:
    case Qt::Key_Enter:
        startCapture(m_tree->currentItem(), qMax(1, m_tree->currentColumn()));
        return true;
    case Qt::Key_Escape:
        emit backRequested();
        return true;
    case Qt::Key_Delete:
        if (!currentId().isEmpty()) emit clearRequested(currentId(), qMax(0, m_tree->currentColumn() - 1));
        return true;
    default:
        return QWidget::eventFilter(watched, event);
    }
}

} // namespace cutenes::app
