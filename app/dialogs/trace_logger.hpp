#pragma once

// Debug > Trace Logger (spec FR-063): starts and stops logging every
// instruction the CPU runs to a file, in the project's trace format (the
// one nestest.log uses, and core.disassemble gives), and shows the file and
// how many instructions it holds. The logging itself is GuiHost's
// (gui.trace_start, gui.trace_stop); the window asks gui.trace_status four
// times a second while shown, so it also shows a trace started through
// automation.

#include "dialogs/cheats_window.hpp" // CommandRunner

#include <QDialog>

class QLabel;
class QPushButton;
class QTimer;

namespace cutenes::app {

class TraceLoggerWindow final : public QDialog {
    Q_OBJECT

public:
    TraceLoggerWindow(CommandRunner run, QWidget* parent = nullptr);

    void refresh();

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    void toggle();

    CommandRunner m_run;
    QPushButton* m_toggle = nullptr;
    QLabel* m_file = nullptr;
    QLabel* m_lines = nullptr;
    QTimer* m_timer = nullptr;
    bool m_active = false;
};

} // namespace cutenes::app
