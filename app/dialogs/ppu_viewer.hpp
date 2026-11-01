#pragma once

// Debug > PPU Viewer (spec US9-4, FR-062): the pattern tables in a chosen
// palette, the four nametables with the scroll the game set, the 64 sprites
// with their OAM bytes, and palette RAM. It fetches the PPU's memory through
// the side-effect-free commands (core.read_vram, core.read_oam,
// core.read_palette, core.read_ppu_registers, core.read_ppu_scroll) and
// draws it with debug/ppu_render; only the tab shown is read, up to 30 times
// a second.

#include "debug/ppu_render.hpp"
#include "dialogs/cheats_window.hpp" // CommandRunner

#include <QDialog>

#include <functional>

class QComboBox;
class QLabel;
class QTabWidget;
class QTableWidget;
class QTimer;

namespace cutenes::app {

class PpuViewerWindow final : public QDialog {
    Q_OBJECT

public:
    // `colours`: the 64 output colours in use (View > Palette).
    PpuViewerWindow(CommandRunner run, std::function<ppu::Colours()> colours, QWidget* parent = nullptr);

    // patterns, nametables, sprites or palette.
    void showView(const QString& view);
    void refresh();

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    // The bytes a refresh gathers, then draws.
    struct Snapshot {
        QByteArray chr;     // $0000-$1FFF
        QByteArray vram;    // $2000-$2FFF
        QByteArray palette; // 32 bytes
        QByteArray oam;     // 256 bytes
        int ctrl = 0;       // PPUCTRL
        int scrollX = 0;
        int scrollY = 0;
    };
    void draw(const Snapshot& s);

    CommandRunner m_run;
    std::function<ppu::Colours()> m_colours;
    QTabWidget* m_tabs = nullptr;
    QComboBox* m_patternPalette = nullptr;
    QLabel* m_patterns[2] = {};
    QLabel* m_nametables = nullptr;
    QLabel* m_scroll = nullptr;
    QLabel* m_sprites = nullptr;
    QTableWidget* m_spriteTable = nullptr;
    QTableWidget* m_paletteTable = nullptr;
    QTimer* m_timer = nullptr;
    bool m_busy = false; // a refresh is in flight: the next waits for it
};

} // namespace cutenes::app
