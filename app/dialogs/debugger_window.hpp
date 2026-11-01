#pragma once

// Debug > Debugger (spec US9-1/2, FR-060): the CPU registers and flags, a
// disassembly from PC, Step Instruction, Step Frame, Run and Pause, and the
// execution breakpoints. Like the cheat windows it is modeless and talks to
// the session only through commands (core.get_cpu, core.disassemble,
// core.set_cpu and the gui.debug_* ones), so an AI's call and a click do the
// same thing. It reads the machine ten times a second while shown.

#include "dialogs/cheats_window.hpp" // CommandRunner, parseNumber

#include <QDialog>

#include <vector>

class QCheckBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QTimer;

namespace cutenes::app {

class DebuggerWindow final : public QDialog {
    Q_OBJECT

public:
    DebuggerWindow(CommandRunner run, QWidget* parent = nullptr);

    // Reads the registers, the disassembly and the breakpoints again.
    void refresh();

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    void showCpu(const api::Json& cpu);
    void showBreakpoints(const api::Json& list);
    void writeRegister(const char* name, QLineEdit* edit, unsigned max);
    void writeFlags();
    void addBreakpoint();
    void toggleBreakpoint(unsigned address);

    CommandRunner m_run;
    QLineEdit* m_a = nullptr;
    QLineEdit* m_x = nullptr;
    QLineEdit* m_y = nullptr;
    QLineEdit* m_s = nullptr;
    QLineEdit* m_p = nullptr;
    QLineEdit* m_pc = nullptr;
    QCheckBox* m_flags[8] = {}; // bit 7 (N) to bit 0 (C)
    QLabel* m_state = nullptr;
    QListWidget* m_listing = nullptr;
    QListWidget* m_breakpointList = nullptr;
    QLineEdit* m_breakpointAddress = nullptr;
    QTimer* m_timer = nullptr;
    std::vector<unsigned> m_breakpoints;
    unsigned m_pValue = 0; // P as last read, for the flag boxes
    bool m_filling = false;
};

} // namespace cutenes::app
