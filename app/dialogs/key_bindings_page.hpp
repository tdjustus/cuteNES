#pragma once

// The Key Bindings page (#108, contracts/menus-and-commands.md "Key
// Bindings page"): a page of the main window listing every game button and
// menu action with its two bindings. Clicking a binding, or Enter on it,
// captures the next key or key combination; Esc cancels the capture, and
// otherwise goes back. The page only asks: the window checks for conflicts
// and runs the gui.* commands, so the page and automation share one path.

#include "settings/key_bindings.hpp"

#include <QWidget>

class QLineEdit;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace cutenes::app {

class KeyBindingsPage : public QWidget {
    Q_OBJECT
public:
    explicit KeyBindingsPage(QWidget* parent = nullptr);

    // Rebuilds the list, keeping the current row and column.
    void setEntries(const QVector<KeyBindings::Entry>& entries);
    // `controls` or `hotkeys`: selects that section's first row.
    void showSection(const QString& section);
    bool isCapturing() const { return m_capture != nullptr; }

signals:
    void bindRequested(const QString& id, int index, const QString& binding);
    void clearRequested(const QString& id, int index);
    void resetRequested(const QString& id); // empty: all
    void backRequested();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void setBindingTexts(QTreeWidgetItem* item, const KeyBindings::Entry& e);
    void startCapture(QTreeWidgetItem* item, int column);
    void endCapture();
    QString currentId() const;
    void updateButtons();
    void applyFilter(const QString& text);

    QLineEdit* m_filter = nullptr;
    QTreeWidget* m_tree = nullptr;
    QPushButton* m_clear = nullptr;
    QPushButton* m_reset = nullptr;
    QVector<KeyBindings::Entry> m_entries;
    QTreeWidgetItem* m_capture = nullptr; // the row being captured
    int m_captureColumn = 0;
};

} // namespace cutenes::app
