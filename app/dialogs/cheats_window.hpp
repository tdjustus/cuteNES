#pragma once

// Tools > Cheats… and Tools > Cheat Search… (spec US8-1..5, FR-050/051/053).
// Both are modeless and talk to the session only through gui.* and core.*
// commands, so the game keeps running while they are open, and every change
// happens at a frame boundary. The search window refreshes its results and
// the watch list ten times a second.

#include <cutenes/api/command.hpp>

#include <QDialog>
#include <QFuture>
#include <QVariantMap>

#include <functional>

class QComboBox;
class QLabel;
class QLineEdit;
class QTableWidget;
class QTimer;

namespace cutenes::app {

// Runs a command on the session (MainWindow::runCommand).
using CommandRunner = std::function<QFuture<api::Result>(const QString& name, const QVariantMap& args)>;

// "$1F", "0x1F" or "31"; nullopt for anything else or above `max`.
std::optional<unsigned> parseNumber(const QString& text, unsigned max);

class CheatsWindow final : public QDialog {
    Q_OBJECT

public:
    CheatsWindow(CommandRunner run, QWidget* parent = nullptr);

    // Reads the list again (after any change).
    void refresh();

private:
    void add();

    CommandRunner m_run;
    QTableWidget* m_table = nullptr;
    QComboBox* m_kind = nullptr;
    QLineEdit* m_address = nullptr;
    QLineEdit* m_value = nullptr;
    QLineEdit* m_code = nullptr;
    QLineEdit* m_label = nullptr;
    bool m_filling = false;
};

class CheatSearchWindow final : public QDialog {
    Q_OBJECT

public:
    CheatSearchWindow(CommandRunner run, QWidget* parent = nullptr);

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    void refresh();
    void apply();
    // Writes `text` at `address` (size bytes) at the next frame boundary.
    void write(unsigned address, int size, const QString& text);
    int selectedAddress() const;

    CommandRunner m_run;
    QComboBox* m_size = nullptr;
    QComboBox* m_filter = nullptr;
    QLineEdit* m_filterValue = nullptr;
    QLabel* m_count = nullptr;
    QTableWidget* m_results = nullptr;
    QTableWidget* m_watches = nullptr;
    QLineEdit* m_watchLabel = nullptr;
    QTimer* m_timer = nullptr;
    int m_searchSize = 1;
    bool m_inFlight = false;
    bool m_filling = false;
};

} // namespace cutenes::app
