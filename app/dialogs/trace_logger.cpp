#include "dialogs/trace_logger.hpp"

#include <QDir>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

namespace cutenes::app {

namespace {

QString instructions(std::uint64_t n)
{
    return n == 1 ? TraceLoggerWindow::tr("1 instruction") : TraceLoggerWindow::tr("%L1 instructions").arg(n);
}

} // namespace

TraceLoggerWindow::TraceLoggerWindow(CommandRunner run, QWidget* parent) : QDialog(parent), m_run(std::move(run))
{
    setObjectName(QStringLiteral("traceLogger"));
    setWindowTitle(tr("Trace Logger"));
    auto* about = new QLabel(tr("Logs every instruction the CPU runs, with its registers, PPU position and cycle "
                                "count, to a file in the library's debug folder. A running game logs about 600,000 "
                                "instructions (some 55 MB) a second, so log only as long as you need."),
        this);
    about->setWordWrap(true);
    m_toggle = new QPushButton(tr("Start Logging"), this);
    m_toggle->setObjectName(QStringLiteral("traceToggle"));
    connect(m_toggle, &QPushButton::clicked, this, &TraceLoggerWindow::toggle);
    m_file = new QLabel(tr("Not logging"), this);
    m_file->setObjectName(QStringLiteral("traceFile"));
    m_file->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_file->setWordWrap(true);
    m_lines = new QLabel(this);
    m_lines->setObjectName(QStringLiteral("traceLines"));

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(about);
    layout->addWidget(m_toggle);
    layout->addWidget(m_file);
    layout->addWidget(m_lines);
    layout->addStretch();
    resize(420, sizeHint().height());

    m_timer = new QTimer(this);
    m_timer->setInterval(250);
    connect(m_timer, &QTimer::timeout, this, &TraceLoggerWindow::refresh);
}

void TraceLoggerWindow::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);
    m_timer->start();
    refresh();
}

void TraceLoggerWindow::hideEvent(QHideEvent* event)
{
    m_timer->stop();
    QDialog::hideEvent(event);
}

void TraceLoggerWindow::refresh()
{
    m_run(QStringLiteral("gui.trace_status"), {}).then(this, [this](const api::Result& r) {
        if (r.isError()) return;
        m_active = r.value.value("active", false);
        m_toggle->setText(m_active ? tr("Stop Logging") : tr("Start Logging"));
        if (!m_active) return; // the last file stays shown
        m_file->setText(QDir::toNativeSeparators(QString::fromStdString(r.value["path"].get<std::string>())));
        m_lines->setText(instructions(r.value["lines"].get<std::uint64_t>()));
    });
}

void TraceLoggerWindow::toggle()
{
    // The result is the file and, on a stop, its count; a refresh follows.
    m_run(m_active ? QStringLiteral("gui.trace_stop") : QStringLiteral("gui.trace_start"), {})
        .then(this, [this](const api::Result& r) {
            if (!r.isError() && r.value.contains("lines")) {
                m_file->setText(tr("Saved %1").arg(QDir::toNativeSeparators(QString::fromStdString(r.value["path"].get<std::string>()))));
                m_lines->setText(instructions(r.value["lines"].get<std::uint64_t>()));
            }
            refresh();
        });
}

} // namespace cutenes::app
