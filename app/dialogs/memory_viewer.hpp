#pragma once

// Debug > Memory Viewer (spec US9-3, FR-061): CPU memory, PPU memory, OAM
// and palette RAM in hex, 16 bytes to a row, editable in place. It reads
// only through the side-effect-free commands (core.peek, core.read_vram,
// core.read_oam, core.read_palette), so looking never changes the game: the
// PPU registers in CPU memory show the core's placeholders, not a read.
// An edit goes through the matching write command, at the next frame
// boundary. Like the other tool windows it is modeless and reads the
// machine ten times a second while shown.

#include "dialogs/cheats_window.hpp" // CommandRunner, parseHex

#include <QByteArray>
#include <QDialog>

class QComboBox;
class QLineEdit;
class QScrollBar;
class QTableWidget;
class QTableWidgetItem;
class QTimer;

namespace cutenes::app {

class MemoryViewerWindow final : public QDialog {
    Q_OBJECT

public:
    MemoryViewerWindow(CommandRunner run, QWidget* parent = nullptr);

    // Reads the rows shown again.
    void refresh();

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    unsigned spaceSize() const; // bytes in the space shown
    int rowsShown() const;
    void spaceChanged();
    void goToAddress();
    void showBytes(unsigned start, const QByteArray& bytes);
    void edited(QTableWidgetItem* item);

    CommandRunner m_run;
    QComboBox* m_space = nullptr;
    QLineEdit* m_address = nullptr;
    QTableWidget* m_table = nullptr;
    QScrollBar* m_scroll = nullptr;
    QTimer* m_timer = nullptr;
    unsigned m_start = 0; // the first address shown, a multiple of 16
    bool m_filling = false;
};

} // namespace cutenes::app
