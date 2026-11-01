#include "dialogs/memory_viewer.hpp"

#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QScrollBar>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace cutenes::app {

namespace {

constexpr int kRows = 16; // 256 bytes at a time
constexpr int kColumns = 16;

QString hex(unsigned value, int digits)
{
    return QStringLiteral("%1").arg(value, digits, 16, QLatin1Char('0')).toUpper();
}

QString base64(const QByteArray& raw)
{
    return QString::fromLatin1(raw.toBase64());
}

QByteArray decoded(const api::Result& r)
{
    return QByteArray::fromBase64(QByteArray::fromStdString(r.value["bytes"].get<std::string>()));
}

} // namespace

MemoryViewerWindow::MemoryViewerWindow(CommandRunner run, QWidget* parent) : QDialog(parent), m_run(std::move(run))
{
    setObjectName(QStringLiteral("memoryViewer"));
    setWindowTitle(tr("Memory Viewer"));

    m_space = new QComboBox(this);
    m_space->setObjectName(QStringLiteral("memorySpace"));
    m_space->addItem(tr("CPU memory ($0000-$FFFF)"), QStringLiteral("cpu"));
    m_space->addItem(tr("PPU memory ($0000-$3FFF)"), QStringLiteral("ppu"));
    m_space->addItem(tr("OAM (sprites, 256 bytes)"), QStringLiteral("oam"));
    m_space->addItem(tr("Palette RAM ($3F00-$3F1F)"), QStringLiteral("palette"));
    connect(m_space, &QComboBox::currentIndexChanged, this, &MemoryViewerWindow::spaceChanged);
    m_address = new QLineEdit(this);
    m_address->setObjectName(QStringLiteral("memoryAddress"));
    m_address->setPlaceholderText(tr("Go to $0000"));
    connect(m_address, &QLineEdit::editingFinished, this, &MemoryViewerWindow::goToAddress);

    QFont fixed(QStringLiteral("monospace"));
    fixed.setStyleHint(QFont::TypeWriter);
    m_table = new QTableWidget(kRows, kColumns, this);
    m_table->setObjectName(QStringLiteral("memoryTable"));
    m_table->setFont(fixed);
    QStringList columns;
    for (int c = 0; c < kColumns; ++c) columns << hex(static_cast<unsigned>(c), 1);
    m_table->setHorizontalHeaderLabels(columns);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_table->verticalHeader()->setFont(fixed);
    m_table->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_table->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed
        | QAbstractItemView::AnyKeyPressed);
    for (int r = 0; r < kRows; ++r) {
        m_table->setVerticalHeaderItem(r, new QTableWidgetItem);
        for (int c = 0; c < kColumns; ++c) m_table->setItem(r, c, new QTableWidgetItem);
    }
    connect(m_table, &QTableWidget::itemChanged, this, &MemoryViewerWindow::edited);

    // The scroll bar moves a row at a time over the whole space.
    m_scroll = new QScrollBar(Qt::Vertical, this);
    m_scroll->setObjectName(QStringLiteral("memoryScroll"));
    m_scroll->setPageStep(kRows);
    connect(m_scroll, &QScrollBar::valueChanged, this, [this](int row) {
        m_start = static_cast<unsigned>(row) * kColumns;
        refresh();
    });

    auto* top = new QHBoxLayout;
    top->addWidget(m_space, 1);
    top->addWidget(m_address);
    auto* body = new QHBoxLayout;
    body->addWidget(m_table, 1);
    body->addWidget(m_scroll);
    auto* layout = new QVBoxLayout(this);
    layout->addLayout(top);
    layout->addLayout(body, 1);
    layout->addWidget(new QLabel(tr("Double-click a byte to change it (hex). Viewing never changes the game."), this));

    m_timer = new QTimer(this);
    m_timer->setInterval(100);
    connect(m_timer, &QTimer::timeout, this, &MemoryViewerWindow::refresh);
    spaceChanged();
}

unsigned MemoryViewerWindow::spaceSize() const
{
    const QString space = m_space->currentData().toString();
    if (space == QLatin1String("cpu")) return 0x10000;
    if (space == QLatin1String("ppu")) return 0x4000;
    if (space == QLatin1String("oam")) return 256;
    return 32;
}

int MemoryViewerWindow::rowsShown() const
{
    return static_cast<int>(std::min<unsigned>(kRows, spaceSize() / kColumns));
}

void MemoryViewerWindow::spaceChanged()
{
    const int rows = rowsShown();
    for (int r = 0; r < kRows; ++r) m_table->setRowHidden(r, r >= rows);
    const QSignalBlocker block(m_scroll);
    m_scroll->setRange(0, static_cast<int>(spaceSize() / kColumns) - rows);
    m_scroll->setValue(0);
    m_scroll->setVisible(m_scroll->maximum() > 0);
    m_start = 0;
    refresh();
}

void MemoryViewerWindow::goToAddress()
{
    const std::optional<unsigned> address = parseHex(m_address->text(), spaceSize() - 1);
    if (!address) {
        m_address->selectAll();
        return;
    }
    // The row holding the address comes first, as far as the space allows.
    const QSignalBlocker block(m_scroll);
    m_scroll->setValue(static_cast<int>(*address / kColumns));
    m_start = static_cast<unsigned>(m_scroll->value()) * kColumns;
    refresh();
}

void MemoryViewerWindow::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);
    m_timer->start();
    refresh();
}

void MemoryViewerWindow::hideEvent(QHideEvent* event)
{
    m_timer->stop();
    QDialog::hideEvent(event);
}

void MemoryViewerWindow::refresh()
{
    const QString space = m_space->currentData().toString();
    const unsigned start = m_start;
    const int length = rowsShown() * kColumns;
    QString command;
    QVariantMap args;
    if (space == QLatin1String("cpu")) {
        command = QStringLiteral("core.peek"); // side-effect-free, unlike core.read_memory
        args = {{QStringLiteral("addr"), start}, {QStringLiteral("length"), length}};
    } else if (space == QLatin1String("ppu")) {
        command = QStringLiteral("core.read_vram");
        args = {{QStringLiteral("addr"), start}, {QStringLiteral("length"), length}};
    } else {
        command = space == QLatin1String("oam") ? QStringLiteral("core.read_oam") : QStringLiteral("core.read_palette");
    }
    m_run(command, args).then(this, [this, space, start, length](const api::Result& r) {
        // A read for what is no longer shown is dropped.
        if (r.isError() || space != m_space->currentData().toString() || start != m_start) return;
        QByteArray raw = decoded(r);
        // OAM and palette RAM come whole.
        if (space == QLatin1String("oam") || space == QLatin1String("palette")) {
            raw = raw.mid(static_cast<qsizetype>(start), length);
        }
        showBytes(start, raw);
    });
}

void MemoryViewerWindow::showBytes(unsigned start, const QByteArray& raw)
{
    m_filling = true;
    for (int r = 0; r < rowsShown(); ++r) {
        m_table->verticalHeaderItem(r)->setText(hex(start + static_cast<unsigned>(r * kColumns), 4));
        for (int c = 0; c < kColumns; ++c) {
            const qsizetype i = r * kColumns + c;
            QTableWidgetItem* item = m_table->item(r, c);
            const QString text = i < raw.size() ? hex(static_cast<unsigned char>(raw[i]), 2) : QString();
            if (item->text() != text) item->setText(text);
        }
    }
    m_filling = false;
}

void MemoryViewerWindow::edited(QTableWidgetItem* item)
{
    if (m_filling) return;
    const std::optional<unsigned> value = parseHex(item->text(), 0xFF);
    const unsigned address = m_start + static_cast<unsigned>(item->row() * kColumns + item->column());
    if (!value) {
        refresh(); // not a byte: the cell shows the memory again
        return;
    }
    const QByteArray byte(1, static_cast<char>(*value));
    const QString space = m_space->currentData().toString();
    const auto done = [this](const api::Result&) { refresh(); };
    if (space == QLatin1String("cpu") || space == QLatin1String("ppu")) {
        m_run(space == QLatin1String("cpu") ? QStringLiteral("core.write_memory") : QStringLiteral("core.write_vram"),
            {{QStringLiteral("addr"), address}, {QStringLiteral("bytes"), base64(byte)}})
            .then(this, done);
        return;
    }
    // OAM and palette RAM are written whole: read, change the byte, write.
    const bool oam = space == QLatin1String("oam");
    m_run(oam ? QStringLiteral("core.read_oam") : QStringLiteral("core.read_palette"), {})
        .then(this, [this, oam, address, byte, done](const api::Result& r) {
            if (r.isError()) return;
            QByteArray block = decoded(r);
            if (static_cast<qsizetype>(address) >= block.size()) return;
            block[static_cast<qsizetype>(address)] = byte[0];
            m_run(oam ? QStringLiteral("core.write_oam") : QStringLiteral("core.write_palette"),
                {{QStringLiteral("bytes"), base64(block)}})
                .then(this, done);
        });
}

} // namespace cutenes::app
