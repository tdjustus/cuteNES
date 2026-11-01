#include "dialogs/ppu_viewer.hpp"

#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPainter>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <memory>

namespace cutenes::app {

namespace {

QString hex(unsigned value, int digits)
{
    return QStringLiteral("%1").arg(value, digits, 16, QLatin1Char('0')).toUpper();
}

QByteArray decoded(const api::Result& r)
{
    return QByteArray::fromBase64(QByteArray::fromStdString(r.value["bytes"].get<std::string>()));
}

// A picture at its own size (the pixmap), shown `scale` times larger.
QLabel* picture(QWidget* parent, const char* name, QSize size, int scale)
{
    auto* label = new QLabel(parent);
    label->setObjectName(QLatin1String(name));
    label->setScaledContents(true);
    label->setFixedSize(size * scale);
    return label;
}

QTableWidget* readOnlyTable(QWidget* parent, const char* name, int rows, int columns)
{
    auto* t = new QTableWidget(rows, columns, parent);
    t->setObjectName(QLatin1String(name));
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < columns; ++c) t->setItem(r, c, new QTableWidgetItem);
    }
    return t;
}

} // namespace

PpuViewerWindow::PpuViewerWindow(CommandRunner run, std::function<ppu::Colours()> colours, QWidget* parent)
    : QDialog(parent), m_run(std::move(run)), m_colours(std::move(colours))
{
    setObjectName(QStringLiteral("ppuViewer"));
    setWindowTitle(tr("PPU Viewer"));
    m_tabs = new QTabWidget(this);
    m_tabs->setObjectName(QStringLiteral("ppuTabs"));

    // Pattern tables, in one of the eight palettes.
    auto* patterns = new QWidget(m_tabs);
    patterns->setObjectName(QStringLiteral("patterns"));
    m_patternPalette = new QComboBox(patterns);
    m_patternPalette->setObjectName(QStringLiteral("patternPalette"));
    for (int p = 0; p < 8; ++p) {
        m_patternPalette->addItem(p < 4 ? tr("Background palette %1").arg(p) : tr("Sprite palette %1").arg(p - 4));
    }
    connect(m_patternPalette, &QComboBox::currentIndexChanged, this, &PpuViewerWindow::refresh);
    m_patterns[0] = picture(patterns, "patternTable0", {128, 128}, 2);
    m_patterns[1] = picture(patterns, "patternTable1", {128, 128}, 2);
    auto* tables = new QHBoxLayout;
    for (int i = 0; i < 2; ++i) {
        auto* column = new QVBoxLayout;
        column->addWidget(new QLabel(i == 0 ? QStringLiteral("$0000") : QStringLiteral("$1000"), patterns));
        column->addWidget(m_patterns[i]);
        tables->addLayout(column);
    }
    auto* patternLayout = new QVBoxLayout(patterns);
    patternLayout->addWidget(m_patternPalette);
    patternLayout->addLayout(tables);
    patternLayout->addStretch();
    m_tabs->addTab(patterns, tr("Pattern Tables"));

    // The four nametables, with the scroll marked.
    auto* names = new QWidget(m_tabs);
    names->setObjectName(QStringLiteral("nametables"));
    m_nametables = picture(names, "nametableView", {512, 480}, 1);
    m_scroll = new QLabel(names);
    m_scroll->setObjectName(QStringLiteral("scrollPosition"));
    auto* nameLayout = new QVBoxLayout(names);
    nameLayout->addWidget(m_scroll);
    nameLayout->addWidget(m_nametables);
    nameLayout->addStretch();
    m_tabs->addTab(names, tr("Nametables"));

    // The sprites, drawn and listed.
    auto* spritePage = new QWidget(m_tabs);
    spritePage->setObjectName(QStringLiteral("sprites"));
    m_sprites = picture(spritePage, "spriteView", {64, 128}, 3);
    m_spriteTable = readOnlyTable(spritePage, "spriteTable", 64, 5);
    m_spriteTable->setHorizontalHeaderLabels({tr("#"), tr("X"), tr("Y"), tr("Tile"), tr("Attributes")});
    m_spriteTable->verticalHeader()->hide();
    auto* spriteLayout = new QHBoxLayout(spritePage);
    spriteLayout->addWidget(m_sprites, 0, Qt::AlignTop);
    spriteLayout->addWidget(m_spriteTable, 1);
    m_tabs->addTab(spritePage, tr("Sprites"));

    // Palette RAM: $3F00-$3F0F the background's, $3F10-$3F1F the sprites'.
    auto* palettePage = new QWidget(m_tabs);
    palettePage->setObjectName(QStringLiteral("palette"));
    m_paletteTable = readOnlyTable(palettePage, "paletteTable", 2, 16);
    m_paletteTable->setVerticalHeaderLabels({QStringLiteral("$3F00"), QStringLiteral("$3F10")});
    auto* paletteLayout = new QVBoxLayout(palettePage);
    paletteLayout->addWidget(m_paletteTable);
    paletteLayout->addStretch();
    m_tabs->addTab(palettePage, tr("Palette"));
    connect(m_tabs, &QTabWidget::currentChanged, this, &PpuViewerWindow::refresh);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_tabs);

    m_timer = new QTimer(this);
    m_timer->setInterval(33); // up to 30 Hz (FR-062)
    connect(m_timer, &QTimer::timeout, this, &PpuViewerWindow::refresh);
}

void PpuViewerWindow::showView(const QString& view)
{
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (m_tabs->widget(i)->objectName() == view) m_tabs->setCurrentIndex(i);
    }
}

void PpuViewerWindow::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);
    m_timer->start();
    refresh();
}

void PpuViewerWindow::hideEvent(QHideEvent* event)
{
    m_timer->stop();
    QDialog::hideEvent(event);
}

void PpuViewerWindow::refresh()
{
    if (m_busy) return;
    m_busy = true;
    // One read after another, then one draw, of what the tab needs: the
    // palette and PPUCTRL always, the pattern tables and nametables but for
    // the palette tab, then OAM (sprites) or the scroll (nametables).
    const QString view = m_tabs->currentWidget()->objectName();
    const int vram = view == QLatin1String("nametables") ? 0x3000 : view == QLatin1String("palette") ? 0 : 0x2000;
    auto s = std::make_shared<Snapshot>();
    m_run(QStringLiteral("core.read_palette"), {})
        .then(this, [this, s](const api::Result& r) {
            if (!r.isError()) s->palette = decoded(r);
            return m_run(QStringLiteral("core.read_ppu_registers"), {});
        })
        .unwrap()
        .then(this, [this, s, vram](const api::Result& r) {
            const QByteArray registers = r.isError() ? QByteArray() : decoded(r);
            s->ctrl = registers.isEmpty() ? 0 : static_cast<unsigned char>(registers[0]);
            if (vram == 0) return QtFuture::makeReadyValueFuture(api::Result::ok());
            return m_run(QStringLiteral("core.read_vram"), {{QStringLiteral("addr"), 0}, {QStringLiteral("length"), vram}});
        })
        .unwrap()
        .then(this, [this, s, view](const api::Result& r) {
            if (!r.isError() && r.value.contains("bytes")) {
                const QByteArray all = decoded(r);
                s->chr = all.left(0x2000);
                s->vram = all.mid(0x2000, 0x1000);
            }
            if (view == QLatin1String("sprites")) return m_run(QStringLiteral("core.read_oam"), {});
            if (view == QLatin1String("nametables")) return m_run(QStringLiteral("core.read_ppu_scroll"), {});
            return QtFuture::makeReadyValueFuture(api::Result::ok());
        })
        .unwrap()
        .then(this, [this, s](const api::Result& r) {
            if (!r.isError() && r.value.contains("bytes")) s->oam = decoded(r);
            if (!r.isError() && r.value.contains("scrollX")) {
                s->scrollX = r.value["scrollX"].get<int>();
                s->scrollY = r.value["scrollY"].get<int>();
            }
            m_busy = false;
            draw(*s);
        })
        .onCanceled(this, [this] { m_busy = false; });
}

void PpuViewerWindow::draw(const Snapshot& s)
{
    const ppu::Colours colours = m_colours();
    const QString view = m_tabs->currentWidget()->objectName();
    if (view == QLatin1String("patterns")) {
        const ppu::Four four = ppu::subPalette(s.palette, m_patternPalette->currentIndex(), colours);
        for (int i = 0; i < 2; ++i) m_patterns[i]->setPixmap(QPixmap::fromImage(ppu::patternTable(s.chr.mid(i * 0x1000, 0x1000), four)));
    } else if (view == QLatin1String("nametables")) {
        // PPUCTRL bit 4: the background's pattern table.
        QImage image = ppu::nametables(s.vram, s.chr.mid(s.ctrl & 0x10 ? 0x1000 : 0, 0x1000), s.palette, colours);
        // The screen the scroll shows, wrapping round the four nametables.
        QPainter painter(&image);
        painter.setPen(QPen(QColor(255, 64, 64), 1));
        for (int dx : {0, -512}) {
            for (int dy : {0, -480}) painter.drawRect(s.scrollX + dx, s.scrollY + dy, 255, 239);
        }
        painter.end();
        m_nametables->setPixmap(QPixmap::fromImage(image));
        m_scroll->setText(tr("Scroll: X %1, Y %2 (nametable %3)")
                .arg(s.scrollX % 256)
                .arg(s.scrollY % 240)
                .arg((s.scrollX >= 256 ? 1 : 0) + (s.scrollY >= 240 ? 2 : 0)));
    } else if (view == QLatin1String("sprites")) {
        const bool tall = s.ctrl & 0x20; // PPUCTRL bit 5
        m_sprites->setPixmap(QPixmap::fromImage(ppu::sprites(s.oam, s.chr, tall, s.ctrl & 0x08 ? 1 : 0, s.palette, colours)));
        for (int i = 0; i < 64 && s.oam.size() >= 256; ++i) {
            const auto at = [&](int k) { return static_cast<unsigned char>(s.oam[i * 4 + k]); };
            const QString cells[5] = {QString::number(i), QString::number(at(3)), QString::number(at(0)), hex(at(1), 2), hex(at(2), 2)};
            for (int c = 0; c < 5; ++c) {
                if (m_spriteTable->item(i, c)->text() != cells[c]) m_spriteTable->item(i, c)->setText(cells[c]);
            }
        }
    } else {
        for (int i = 0; i < 32 && i < s.palette.size(); ++i) {
            const unsigned value = static_cast<unsigned char>(s.palette[i]);
            QTableWidgetItem* item = m_paletteTable->item(i / 16, i % 16);
            item->setText(hex(value, 2));
            const QColor colour = QColor::fromRgb(colours[value & 0x3F] & 0xFFFFFF);
            item->setBackground(colour);
            item->setForeground(colour.lightness() > 128 ? Qt::black : Qt::white);
        }
    }
}

} // namespace cutenes::app
