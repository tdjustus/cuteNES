#include "dialogs/debugger_window.hpp"

#include <QCheckBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace cutenes::app {

namespace {

// The disassembly shown from PC.
constexpr int kLines = 24;

QString hex(unsigned value, int digits)
{
    return QStringLiteral("%1").arg(value, digits, 16, QLatin1Char('0')).toUpper();
}

QLineEdit* registerEdit(QWidget* parent, const char* name, int digits)
{
    auto* edit = new QLineEdit(parent);
    edit->setObjectName(QStringLiteral("register") + QLatin1String(name));
    edit->setMaxLength(digits + 2);
    edit->setFixedWidth(edit->fontMetrics().horizontalAdvance(QLatin1Char('0')) * (digits + 4));
    return edit;
}

QFont fixedFont()
{
    QFont font(QStringLiteral("monospace"));
    font.setStyleHint(QFont::TypeWriter);
    return font;
}

} // namespace

DebuggerWindow::DebuggerWindow(CommandRunner run, QWidget* parent) : QDialog(parent), m_run(std::move(run))
{
    setObjectName(QStringLiteral("debuggerWindow"));
    setWindowTitle(tr("Debugger"));

    m_a = registerEdit(this, "A", 2);
    m_x = registerEdit(this, "X", 2);
    m_y = registerEdit(this, "Y", 2);
    m_s = registerEdit(this, "S", 2);
    m_p = registerEdit(this, "P", 2);
    m_pc = registerEdit(this, "PC", 4);
    connect(m_a, &QLineEdit::editingFinished, this, [this] { writeRegister("a", m_a, 0xFF); });
    connect(m_x, &QLineEdit::editingFinished, this, [this] { writeRegister("x", m_x, 0xFF); });
    connect(m_y, &QLineEdit::editingFinished, this, [this] { writeRegister("y", m_y, 0xFF); });
    connect(m_s, &QLineEdit::editingFinished, this, [this] { writeRegister("s", m_s, 0xFF); });
    connect(m_p, &QLineEdit::editingFinished, this, [this] { writeRegister("p", m_p, 0xFF); });
    connect(m_pc, &QLineEdit::editingFinished, this, [this] { writeRegister("pc", m_pc, 0xFFFF); });
    auto* registers = new QFormLayout;
    registers->addRow(QStringLiteral("PC"), m_pc);
    registers->addRow(QStringLiteral("A"), m_a);
    registers->addRow(QStringLiteral("X"), m_x);
    registers->addRow(QStringLiteral("Y"), m_y);
    registers->addRow(QStringLiteral("S"), m_s);
    registers->addRow(QStringLiteral("P"), m_p);

    // N V - B D I Z C. Bit 5 is always set, and B exists only on the stack.
    auto* flags = new QGridLayout;
    const char* names = "NV-BDIZC";
    for (int i = 0; i < 8; ++i) {
        auto* box = new QCheckBox(QString(QLatin1Char(names[i])), this);
        box->setObjectName(QStringLiteral("flag") + QLatin1Char(names[i]));
        box->setEnabled(names[i] != '-' && names[i] != 'B');
        connect(box, &QCheckBox::toggled, this, [this] { if (!m_filling) writeFlags(); });
        m_flags[i] = box;
        flags->addWidget(box, i / 4, i % 4);
    }
    m_state = new QLabel(this);
    m_state->setObjectName(QStringLiteral("runState"));

    const auto button = [this](const QString& text, const char* name, const QString& command) {
        auto* b = new QPushButton(text, this);
        b->setObjectName(QLatin1String(name));
        b->setAutoDefault(false);
        connect(b, &QPushButton::clicked, this, [this, command] { m_run(command, {}).then(this, [this](const api::Result&) { refresh(); }); });
        return b;
    };
    auto* controls = new QHBoxLayout;
    controls->addWidget(button(tr("Step Instruction"), "stepInstruction", QStringLiteral("gui.debug_step_instruction")));
    controls->addWidget(button(tr("Step Frame"), "stepFrame", QStringLiteral("gui.debug_step_frame")));
    controls->addWidget(button(tr("Run"), "run", QStringLiteral("gui.debug_run")));
    controls->addWidget(button(tr("Pause"), "pause", QStringLiteral("core.pause")));
    controls->addStretch();

    // The listing: from PC, with breakpoints marked. Double-click toggles one.
    m_listing = new QListWidget(this);
    m_listing->setObjectName(QStringLiteral("disassembly"));
    m_listing->setFont(fixedFont());
    m_listing->setMinimumWidth(m_listing->fontMetrics().horizontalAdvance(QLatin1Char('0')) * 52);
    connect(m_listing, &QListWidget::itemDoubleClicked, this,
        [this](QListWidgetItem* item) { toggleBreakpoint(item->data(Qt::UserRole).toUInt()); });

    m_breakpointList = new QListWidget(this);
    m_breakpointList->setObjectName(QStringLiteral("breakpoints"));
    m_breakpointList->setFont(fixedFont());
    m_breakpointAddress = new QLineEdit(this);
    m_breakpointAddress->setObjectName(QStringLiteral("breakpointAddress"));
    m_breakpointAddress->setPlaceholderText(QStringLiteral("$C000"));
    connect(m_breakpointAddress, &QLineEdit::returnPressed, this, &DebuggerWindow::addBreakpoint);
    auto* add = new QPushButton(tr("Add"), this);
    add->setObjectName(QStringLiteral("addBreakpoint"));
    add->setAutoDefault(false);
    connect(add, &QPushButton::clicked, this, &DebuggerWindow::addBreakpoint);
    auto* remove = new QPushButton(tr("Remove"), this);
    remove->setObjectName(QStringLiteral("removeBreakpoint"));
    remove->setAutoDefault(false);
    connect(remove, &QPushButton::clicked, this, [this] {
        if (QListWidgetItem* item = m_breakpointList->currentItem()) toggleBreakpoint(item->data(Qt::UserRole).toUInt());
    });
    auto* breakpointRow = new QHBoxLayout;
    breakpointRow->addWidget(m_breakpointAddress);
    breakpointRow->addWidget(add);
    breakpointRow->addWidget(remove);

    auto* side = new QVBoxLayout;
    side->addLayout(registers);
    side->addLayout(flags);
    side->addWidget(m_state);
    side->addWidget(new QLabel(tr("Breakpoints"), this));
    side->addWidget(m_breakpointList, 1);
    side->addLayout(breakpointRow);
    auto* body = new QHBoxLayout;
    body->addWidget(m_listing, 1);
    body->addLayout(side);
    auto* layout = new QVBoxLayout(this);
    layout->addLayout(controls);
    layout->addLayout(body, 1);

    m_timer = new QTimer(this);
    m_timer->setInterval(100);
    connect(m_timer, &QTimer::timeout, this, &DebuggerWindow::refresh);
}

void DebuggerWindow::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);
    m_timer->start();
    refresh();
}

void DebuggerWindow::hideEvent(QHideEvent* event)
{
    m_timer->stop();
    QDialog::hideEvent(event);
}

void DebuggerWindow::refresh()
{
    m_run(QStringLiteral("gui.list_breakpoints"), {}).then(this, [this](const api::Result& r) {
        if (!r.isError()) showBreakpoints(r.value["breakpoints"]);
    });
    m_run(QStringLiteral("core.run_state"), {}).then(this, [this](const api::Result& r) {
        if (!r.isError()) m_state->setText(QString::fromStdString(r.value["runState"].get<std::string>()));
    });
    m_run(QStringLiteral("core.get_cpu"), {}).then(this, [this](const api::Result& r) {
        if (r.isError()) return;
        showCpu(r.value["cpu"]);
        const unsigned pc = r.value["cpu"]["pc"].get<unsigned>();
        m_run(QStringLiteral("core.disassemble"), {{QStringLiteral("addr"), pc}, {QStringLiteral("count"), kLines}})
            .then(this, [this](const api::Result& d) {
                if (d.isError()) return;
                const api::Json& lines = d.value["lines"];
                const int row = m_listing->currentRow();
                m_listing->clear();
                for (const api::Json& line : lines) {
                    const unsigned address = line["addr"].get<unsigned>();
                    const bool marked = std::find(m_breakpoints.begin(), m_breakpoints.end(), address) != m_breakpoints.end();
                    // The trace format's first columns: address, bytes, instruction.
                    const QString text = QString::fromStdString(line["text"].get<std::string>()).left(47).trimmed();
                    // Marks: ▶ the next instruction (the first line, PC), ● a breakpoint.
                    const bool next = m_listing->count() == 0;
                    QString marks = QString(next ? QStringLiteral("▶") : QStringLiteral(" "))
                        + (marked ? QStringLiteral("●") : QStringLiteral(" ")) + QLatin1Char(' ');
                    auto* item = new QListWidgetItem(marks + text);
                    item->setData(Qt::UserRole, address);
                    if (next) {
                        QFont bold = item->font();
                        bold.setBold(true);
                        item->setFont(bold);
                    }
                    m_listing->addItem(item);
                }
                if (row >= 0 && row < m_listing->count()) m_listing->setCurrentRow(row);
            });
    });
}

void DebuggerWindow::showCpu(const api::Json& cpu)
{
    m_filling = true;
    const auto set = [](QLineEdit* edit, unsigned value, int digits) {
        if (!edit->isModified()) edit->setText(hex(value, digits)); // never over what the user is typing
    };
    set(m_a, cpu["a"].get<unsigned>(), 2);
    set(m_x, cpu["x"].get<unsigned>(), 2);
    set(m_y, cpu["y"].get<unsigned>(), 2);
    set(m_s, cpu["s"].get<unsigned>(), 2);
    set(m_p, cpu["p"].get<unsigned>(), 2);
    set(m_pc, cpu["pc"].get<unsigned>(), 4);
    m_pValue = cpu["p"].get<unsigned>();
    for (int i = 0; i < 8; ++i) m_flags[i]->setChecked(m_pValue & (0x80u >> i));
    m_filling = false;
}

void DebuggerWindow::showBreakpoints(const api::Json& list)
{
    m_breakpoints.clear();
    for (const api::Json& a : list) m_breakpoints.push_back(a.get<unsigned>());
    const int row = m_breakpointList->currentRow();
    m_breakpointList->clear();
    for (const unsigned address : m_breakpoints) {
        auto* item = new QListWidgetItem(QLatin1Char('$') + hex(address, 4));
        item->setData(Qt::UserRole, address);
        m_breakpointList->addItem(item);
    }
    if (row >= 0 && row < m_breakpointList->count()) m_breakpointList->setCurrentRow(row);
}

void DebuggerWindow::writeRegister(const char* name, QLineEdit* edit, unsigned max)
{
    // Only what the user typed: a field also finishes editing when it loses
    // focus, the window's closing included.
    if (!edit->isModified()) return;
    const std::optional<unsigned> value = parseHex(edit->text(), max);
    if (!value) {
        edit->selectAll(); // not a value: left for the user to correct
        return;
    }
    edit->setModified(false); // the CPU's value shows again from the next read
    m_run(QStringLiteral("core.set_cpu"), {{QLatin1String(name), *value}}).then(this, [this](const api::Result&) { refresh(); });
}

void DebuggerWindow::writeFlags()
{
    unsigned p = m_pValue & 0x30; // bit 5 and B as they are
    for (int i = 0; i < 8; ++i) {
        if (m_flags[i]->isEnabled() && m_flags[i]->isChecked()) p |= 0x80u >> i;
    }
    m_run(QStringLiteral("core.set_cpu"), {{QStringLiteral("p"), p}}).then(this, [this](const api::Result&) { refresh(); });
}

void DebuggerWindow::addBreakpoint()
{
    const std::optional<unsigned> address = parseHex(m_breakpointAddress->text(), 0xFFFF);
    if (!address) {
        m_breakpointAddress->selectAll();
        return;
    }
    m_breakpointAddress->clear();
    m_run(QStringLiteral("gui.add_breakpoint"), {{QStringLiteral("address"), *address}})
        .then(this, [this](const api::Result&) { refresh(); });
}

void DebuggerWindow::toggleBreakpoint(unsigned address)
{
    const bool set = std::find(m_breakpoints.begin(), m_breakpoints.end(), address) != m_breakpoints.end();
    m_run(set ? QStringLiteral("gui.remove_breakpoint") : QStringLiteral("gui.add_breakpoint"),
        {{QStringLiteral("address"), address}})
        .then(this, [this](const api::Result&) { refresh(); });
}

} // namespace cutenes::app
