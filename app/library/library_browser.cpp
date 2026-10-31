#include "library/library_browser.hpp"

#include <QAction>
#include <QActionGroup>
#include <QButtonGroup>
#include <QDir>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QLocale>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QResizeEvent>
#include <QRegularExpression>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace cutenes::app {

// --- LibraryModel -------------------------------------------------------------

void LibraryModel::setEntries(QVector<LibraryEntry> entries)
{
    beginResetModel();
    m_entries = std::move(entries);
    endResetModel();
}

int LibraryModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_entries.size());
}

int LibraryModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant LibraryModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= m_entries.size()) return {};
    const LibraryEntry& e = m_entries[index.row()];
    switch (role) {
    case PathRole: return e.path;
    case IdentityRole: return e.identity;
    case RecentRole: return e.recent;
    case Qt::ToolTipRole: return QDir::toNativeSeparators(e.path) + (e.supported ? QString() : QStringLiteral("\n") + e.reason);
    case Qt::ForegroundRole:
        if (!e.supported) return QPalette().brush(QPalette::Disabled, QPalette::Text);
        return {};
    case SortRole:
        switch (index.column()) {
        case Mapper: return e.mapper;
        case States: return e.stateCount;
        case Save: return e.hasSave;
        case LastPlayed: return e.lastPlayed.isValid() ? e.lastPlayed.toMSecsSinceEpoch() : qint64(0);
        default: return data(index, Qt::DisplayRole).toString().toLower();
        }
    case Qt::DisplayRole:
        switch (index.column()) {
        case Name: return e.name;
        case Mapper: return e.mapper < 0 ? QString() : QString::number(e.mapper);
        case Region: return e.region;
        case Status: return e.supported ? QString() : e.reason;
        case Save: return e.hasSave ? tr("Yes") : QString();
        case States: return e.stateCount > 0 ? QString::number(e.stateCount) : QString();
        case LastPlayed:
            return e.lastPlayed.isValid()
                ? QLocale().toString(e.lastPlayed.toLocalTime().date(), QLocale::ShortFormat)
                : QString();
        }
        break;
    default: break;
    }
    return {};
}

QVariant LibraryModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return {};
    switch (section) {
    case Name: return tr("Name");
    case Mapper: return tr("Mapper");
    case Region: return tr("Region");
    case Status: return tr("Status");
    case Save: return tr("Save");
    case States: return tr("States");
    case LastPlayed: return tr("Last Played");
    }
    return {};
}

// --- LibraryFilter ------------------------------------------------------------

void LibraryFilter::setRecentOnly(bool recentOnly)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    beginFilterChange();
    m_recentOnly = recentOnly;
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
#else
    m_recentOnly = recentOnly;
    invalidateFilter();
#endif
}

bool LibraryFilter::filterAcceptsRow(int row, const QModelIndex& parent) const
{
    const QModelIndex index = sourceModel()->index(row, LibraryModel::Name, parent);
    if (m_recentOnly && !index.data(LibraryModel::RecentRole).toBool()) return false;
    return QSortFilterProxyModel::filterAcceptsRow(row, parent);
}

// --- Look -------------------------------------------------------------------

namespace {

// Card geometry: a 4:3 cover with the title and a detail line below it.
constexpr int kCardMinWidth = 164;
constexpr int kGap = 20;       // between cards
constexpr int kTextHeight = 50;
constexpr qreal kRadius = 10;
constexpr int kMargin = 24;    // the page's side margins

QString translate(const char* text, int n = -1)
{
    return QCoreApplication::translate("cutenes::app::LibraryBrowser", text, nullptr, n);
}

QColor mix(const QColor& a, const QColor& b, qreal t)
{
    return QColor::fromRgbF(float(a.redF() + (b.redF() - a.redF()) * t), float(a.greenF() + (b.greenF() - a.greenF()) * t),
        float(a.blueF() + (b.blueF() - a.blueF()) * t));
}

bool isDark(const QPalette& palette)
{
    return palette.color(QPalette::Window).lightness() < 128;
}

QColor secondaryText(const QPalette& palette)
{
    return mix(palette.color(QPalette::WindowText), palette.color(QPalette::Window), 0.42);
}

// "Played yesterday", "Played 3 days ago", …
QString playedText(const QDateTime& when)
{
    if (!when.isValid()) return translate("Not played yet");
    const qint64 days = when.toLocalTime().date().daysTo(QDate::currentDate());
    if (days <= 0) return translate("Played today");
    if (days == 1) return translate("Played yesterday");
    if (days < 7) return translate("Played %1 days ago").arg(days);
    return translate("Played %1").arg(QLocale().toString(when.toLocalTime().date(), QLocale::ShortFormat));
}

// One or two letters for the cover: the initials of the first two words
// that count ("The Legend of Zelda" is "LZ").
QString monogram(const QString& name)
{
    static const QStringList skip = {QStringLiteral("the"), QStringLiteral("a"), QStringLiteral("an"),
        QStringLiteral("of"), QStringLiteral("and")};
    QString letters;
    for (const QString& word : name.split(QRegularExpression(QStringLiteral("[^\\w]+")), Qt::SkipEmptyParts)) {
        if (skip.contains(word.toLower()) && name.split(QLatin1Char(' ')).size() > 1) continue;
        letters += word.at(0).toUpper();
        if (letters.size() == 2) break;
    }
    return letters.isEmpty() ? QStringLiteral("?") : letters;
}

// A small pill of text, anchored at a corner of `area`.
void drawChip(QPainter* p, const QRectF& area, Qt::Alignment corner, const QString& text, const QColor& background,
    const QColor& foreground)
{
    QFont font = p->font();
    font.setPixelSize(10);
    font.setWeight(QFont::Bold);
    font.setLetterSpacing(QFont::AbsoluteSpacing, 0.4);
    const QFontMetricsF metrics(font);
    const QSizeF size(metrics.horizontalAdvance(text) + 12, 18);
    const qreal inset = 8;
    const qreal x = corner & Qt::AlignRight ? area.right() - inset - size.width() : area.left() + inset;
    const qreal y = corner & Qt::AlignBottom ? area.bottom() - inset - size.height() : area.top() + inset;
    const QRectF chip(QPointF(x, y), size);
    p->setPen(Qt::NoPen);
    p->setBrush(background);
    p->drawRoundedRect(chip, size.height() / 2, size.height() / 2);
    p->setFont(font);
    p->setPen(foreground);
    p->drawText(chip, Qt::AlignCenter, text);
}

// Line icons for the toolbar, drawn so they follow the palette.
QIcon lineIcon(const QString& kind, const QColor& color)
{
    const qreal dpr = 2;
    QPixmap pixmap(int(16 * dpr), int(16 * dpr));
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    QPen pen(color, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    if (kind == QLatin1String("search")) {
        p.drawEllipse(QRectF(2.5, 2.5, 8.5, 8.5));
        p.drawLine(QPointF(10, 10), QPointF(13.5, 13.5));
    } else if (kind == QLatin1String("grid")) {
        for (const QPointF& at : {QPointF(2, 2), QPointF(9, 2), QPointF(2, 9), QPointF(9, 9)}) {
            p.drawRoundedRect(QRectF(at, QSizeF(5, 5)), 1.2, 1.2);
        }
    } else if (kind == QLatin1String("list")) {
        for (qreal y : {3.5, 8.0, 12.5}) {
            p.drawPoint(QPointF(2.5, y));
            p.drawLine(QPointF(6, y), QPointF(14, y));
        }
    } else if (kind == QLatin1String("plus")) {
        p.drawLine(QPointF(8, 3), QPointF(8, 13));
        p.drawLine(QPointF(3, 8), QPointF(13, 8));
    } else if (kind == QLatin1String("chevron")) {
        p.drawPolyline(QPolygonF({QPointF(4.5, 6.5), QPointF(8, 10), QPointF(11.5, 6.5)}));
    }
    return QIcon(pixmap);
}

// The empty library's picture: a game controller.
QPixmap controllerPicture(const QPalette& palette)
{
    const qreal dpr = 2;
    QPixmap pixmap(int(132 * dpr), int(72 * dpr));
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    const QColor body = mix(palette.color(QPalette::Window), palette.color(QPalette::WindowText), 0.10);
    const QColor detail = mix(palette.color(QPalette::Window), palette.color(QPalette::WindowText), 0.32);
    p.setPen(Qt::NoPen);
    p.setBrush(body);
    p.drawRoundedRect(QRectF(2, 8, 128, 58), 14, 14);
    p.setBrush(detail);
    // The D-pad.
    p.drawRoundedRect(QRectF(22, 31, 26, 9), 2, 2);
    p.drawRoundedRect(QRectF(30.5, 22.5, 9, 26), 2, 2);
    // Select and Start.
    p.drawRoundedRect(QRectF(56, 38, 9, 4), 2, 2);
    p.drawRoundedRect(QRectF(68, 38, 9, 4), 2, 2);
    // B and A.
    p.setBrush(palette.color(QPalette::Active, QPalette::Highlight));
    p.drawEllipse(QRectF(86, 32, 12, 12));
    p.drawEllipse(QRectF(104, 32, 12, 12));
    return pixmap;
}

// The grid's cells share out the width, so cards fill each row.
class GameGrid final : public QListView {
public:
    explicit GameGrid(QWidget* parent) : QListView(parent)
    {
        setViewMode(QListView::IconMode);
        setFlow(QListView::LeftToRight);
        setWrapping(true);
        setResizeMode(QListView::Adjust);
        setMovement(QListView::Static);
        setUniformItemSizes(true);
        setSelectionMode(QAbstractItemView::SingleSelection);
        setSelectionBehavior(QAbstractItemView::SelectRows); // the list shares the selection
        setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        setMouseTracking(true);
        setFrameShape(QFrame::NoFrame);
        setAttribute(Qt::WA_MacShowFocusRect, false);
        updateGrid();
    }

protected:
    void resizeEvent(QResizeEvent* event) override
    {
        updateGrid(event->size().width());
        QListView::resizeEvent(event);
    }

private:
    void updateGrid(int viewportWidth = 0)
    {
        const int width = std::max(viewportWidth, kCardMinWidth + kGap);
        const int columns = std::max(1, width / (kCardMinWidth + kGap));
        // A pixel short of the width: QListView wraps a row that fills it
        // exactly.
        const int cell = (width - 2) / columns;
        const int card = cell - kGap;
        const QSize size(cell, int(std::round(card * 0.75)) + kTextHeight + kGap);
        if (gridSize() != size) setGridSize(size);
    }
};

// Paints a game as a card: a cover in the game's own colours with its
// initials, region and save badges, then the title and when it was played.
class CardDelegate final : public QStyledItemDelegate {
public:
    CardDelegate(const LibraryFilter* proxy, const LibraryModel* model, QListView* view)
        : QStyledItemDelegate(view), m_proxy(proxy), m_model(model), m_view(view)
    {
    }

    QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const override { return m_view->gridSize(); }

    void paint(QPainter* p, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        const int row = m_proxy->mapToSource(index).row();
        if (row < 0 || row >= m_model->entries().size()) return;
        const LibraryEntry& e = m_model->entries()[row];
        const QPalette& palette = option.palette;
        const bool selected = option.state & QStyle::State_Selected;
        const bool hovered = option.state & QStyle::State_MouseOver;

        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        const QRectF card = QRectF(option.rect).adjusted(kGap / 2.0, kGap / 2.0, -kGap / 2.0, -kGap / 2.0);
        const QRectF cover(card.topLeft(), QSizeF(card.width(), std::round(card.width() * 0.75)));

        // A soft shadow, deeper under the pointer.
        p->setPen(Qt::NoPen);
        for (int i = 1; i <= 4; ++i) {
            p->setBrush(QColor(0, 0, 0, hovered || selected ? 16 : 9));
            p->drawRoundedRect(cover.adjusted(-i * 0.5, i * 0.8, i * 0.5, i * 1.2), kRadius + i, kRadius + i);
        }

        // The cover: a gradient from the game's identity, so a game keeps
        // its colours, with a grey one for a game the core cannot run.
        const auto seed = qHash(e.identity.isEmpty() ? e.name : e.identity);
        const int hue = static_cast<int>(seed % 360);
        const int saturation = e.supported ? 150 : 0;
        QLinearGradient gradient(cover.topLeft(), cover.bottomRight());
        gradient.setColorAt(0, QColor::fromHsl(hue, saturation, 128));
        gradient.setColorAt(1, QColor::fromHsl((hue + 40) % 360, saturation, 64));
        QPainterPath shape;
        shape.addRoundedRect(cover, kRadius, kRadius);
        p->fillPath(shape, gradient);

        p->save();
        p->setClipPath(shape);
        QRadialGradient glow(cover.topLeft() + QPointF(cover.width() * 0.25, 0), cover.width() * 0.9);
        glow.setColorAt(0, QColor(255, 255, 255, 70));
        glow.setColorAt(1, QColor(255, 255, 255, 0));
        p->fillRect(cover, glow);
        // A staircase of big pixels in the corner, for an 8-bit feel.
        const qreal pixel = cover.width() / 14;
        p->setBrush(QColor(255, 255, 255, 22));
        for (int step = 0; step < 4; ++step) {
            for (int i = 0; i <= step; ++i) {
                p->drawRect(QRectF(cover.right() - pixel * (4 - step), cover.bottom() - pixel * (i + 1), pixel, pixel));
            }
        }
        QFont initials = option.font;
        initials.setPixelSize(int(cover.height() * 0.36));
        initials.setWeight(QFont::Black);
        p->setFont(initials);
        p->setPen(QColor(255, 255, 255, 235));
        p->drawText(cover, Qt::AlignCenter, monogram(e.name));
        p->restore();

        const QColor chipBack(0, 0, 0, 110);
        if (!e.region.isEmpty() && e.region != QLatin1String("Unspecified")) {
            drawChip(p, cover, Qt::AlignTop | Qt::AlignLeft, e.region, chipBack, Qt::white);
        }
        if (e.hasSave) drawChip(p, cover, Qt::AlignTop | Qt::AlignRight, translate("SAVE"), chipBack, Qt::white);
        if (!e.supported) {
            drawChip(p, cover, Qt::AlignBottom | Qt::AlignLeft, translate("UNSUPPORTED"), QColor(200, 40, 40, 220), Qt::white);
        }

        // Selected: a ring in the accent colour; under the pointer: a
        // fainter one.
        if (selected || hovered) {
            QColor ring = palette.color(QPalette::Active, QPalette::Highlight);
            if (!selected) ring.setAlpha(110);
            p->setPen(QPen(ring, selected ? 3 : 2));
            p->setBrush(Qt::NoBrush);
            const qreal out = selected ? 3.5 : 3;
            p->drawRoundedRect(cover.adjusted(-out, -out, out, out), kRadius + out, kRadius + out);
        }

        // The title and the detail line.
        QFont title = option.font;
        title.setWeight(QFont::DemiBold);
        const QFontMetricsF titleMetrics(title);
        const QRectF titleRect(card.left() + 2, cover.bottom() + 10, card.width() - 4, titleMetrics.height());
        p->setFont(title);
        p->setPen(palette.color(QPalette::WindowText));
        p->drawText(titleRect, Qt::AlignLeft | Qt::AlignVCenter,
            titleMetrics.elidedText(e.name, Qt::ElideRight, titleRect.width()));

        QFont detail = option.font;
        detail.setPointSizeF(option.font.pointSizeF() * 0.88);
        const QFontMetricsF detailMetrics(detail);
        QString text = e.supported ? playedText(e.lastPlayed) : e.reason;
        if (e.supported && e.stateCount > 0) text += QStringLiteral(" · ") + (e.stateCount == 1 ? translate("1 state") : translate("%1 states").arg(e.stateCount));
        const QRectF detailRect(titleRect.left(), titleRect.bottom() + 2, titleRect.width(), detailMetrics.height());
        p->setFont(detail);
        p->setPen(e.supported ? secondaryText(palette) : QColor(isDark(palette) ? 0xff6b6b : 0xc62828));
        p->drawText(detailRect, Qt::AlignLeft | Qt::AlignVCenter,
            detailMetrics.elidedText(text, Qt::ElideRight, detailRect.width()));
        p->restore();
    }

private:
    const LibraryFilter* m_proxy;
    const LibraryModel* m_model;
    QListView* m_view;
};

} // namespace

// --- LibraryBrowser -----------------------------------------------------------

LibraryBrowser::LibraryBrowser(QWidget* parent)
    : QWidget(parent), m_model(new LibraryModel(this)), m_proxy(new LibraryFilter(this))
{
    setObjectName(QStringLiteral("libraryBrowser"));
    setAttribute(Qt::WA_StyledBackground);
    m_proxy->setSourceModel(m_model);
    m_proxy->setFilterKeyColumn(LibraryModel::Name);
    m_proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
    m_proxy->setSortRole(LibraryModel::SortRole);
    m_proxy->setSortCaseSensitivity(Qt::CaseInsensitive);

    QWidget* header = createHeader();

    m_grid = new GameGrid(this);
    m_grid->setObjectName(QStringLiteral("libraryGrid"));
    m_grid->setModel(m_proxy);
    m_grid->setItemDelegate(new CardDelegate(m_proxy, m_model, m_grid));

    m_list = new QTreeView(this);
    m_list->setObjectName(QStringLiteral("libraryList"));
    m_list->setModel(m_proxy);
    // One selection for both views, so switching keeps the game selected.
    QItemSelectionModel* unused = m_list->selectionModel();
    m_list->setSelectionModel(m_grid->selectionModel());
    delete unused;
    m_list->setRootIsDecorated(false);
    m_list->setUniformRowHeights(true);
    m_list->setAllColumnsShowFocus(true);
    m_list->setSortingEnabled(true);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->setAttribute(Qt::WA_MacShowFocusRect, false);
    m_list->header()->setStretchLastSection(false);
    m_list->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_list->header()->setSectionResizeMode(LibraryModel::Name, QHeaderView::Stretch);
    m_list->header()->setMinimumSectionSize(64);
    m_list->header()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_list->header()->setHighlightSections(false);
    connect(m_list->header(), &QHeaderView::sortIndicatorChanged, this, [this](int column, Qt::SortOrder) {
        m_sort->setText(m_model->headerData(column, Qt::Horizontal, Qt::DisplayRole).toString());
    });

    for (QAbstractItemView* view : {static_cast<QAbstractItemView*>(m_grid), static_cast<QAbstractItemView*>(m_list)}) {
        view->installEventFilter(this);
        connect(view, &QAbstractItemView::activated, this, &LibraryBrowser::activate);
    }

    // The grid's cards inset by half a gap, so they line up with the
    // header's margin.
    auto* gridPage = new QWidget(this);
    auto* gridLayout = new QVBoxLayout(gridPage);
    gridLayout->setContentsMargins(kMargin - kGap / 2, 0, kMargin - kGap / 2 - 4, 0);
    gridLayout->addWidget(m_grid);
    auto* listPage = new QWidget(this);
    auto* listLayout = new QVBoxLayout(listPage);
    listLayout->setContentsMargins(kMargin - 6, 4, kMargin - 6, 0);
    listLayout->addWidget(m_list);

    m_noMatch = new QLabel(this);
    m_noMatch->setObjectName(QStringLiteral("libraryNoMatch"));
    m_noMatch->setAlignment(Qt::AlignCenter);
    m_noMatch->setWordWrap(true);

    m_pages = new QStackedWidget(this);
    m_pages->addWidget(gridPage);
    m_pages->addWidget(listPage);
    m_pages->addWidget(createEmptyPage());
    m_pages->addWidget(m_noMatch);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(header);
    layout->addWidget(m_pages, 1);

    sortBy(LibraryModel::Name, Qt::AscendingOrder);
    setLibraryRoot({});
    updateStyle();
    updateState();
}

QWidget* LibraryBrowser::createHeader()
{
    auto* header = new QWidget(this);
    auto* title = new QLabel(tr("Library"), header);
    title->setObjectName(QStringLiteral("libraryTitle"));
    QFont big = title->font();
    big.setPointSizeF(big.pointSizeF() * 2.0);
    big.setWeight(QFont::Bold);
    title->setFont(big);
    m_count = new QLabel(header);
    m_count->setObjectName(QStringLiteral("libraryCount"));

    m_filter = new QLineEdit(header);
    m_filter->setObjectName(QStringLiteral("libraryFilter"));
    m_filter->setPlaceholderText(tr("Search games"));
    m_filter->setClearButtonEnabled(true);
    m_filter->setFixedWidth(260);
    m_filter->setAttribute(Qt::WA_MacShowFocusRect, false);
    m_filter->addAction(new QAction(m_filter), QLineEdit::LeadingPosition); // the icon (updateStyle)
    m_filter->installEventFilter(this);
    connect(m_filter, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_proxy->setFilterFixedString(text);
        if (!view()->currentIndex().isValid() && m_proxy->rowCount() > 0) {
            view()->setCurrentIndex(m_proxy->index(0, 0));
        }
        updateState();
    });
    connect(m_filter, &QLineEdit::returnPressed, this, [this] { activate(view()->currentIndex()); });

    // A segmented control: one button of each pair is always on.
    auto segmented = [header](QToolButton*& first, const QString& firstText, QToolButton*& second,
                         const QString& secondText, const char* name) {
        auto* frame = new QFrame(header);
        frame->setObjectName(QLatin1String(name));
        frame->setProperty("segmented", true);
        auto* row = new QHBoxLayout(frame);
        row->setContentsMargins(2, 2, 2, 2);
        row->setSpacing(2);
        auto* group = new QButtonGroup(frame);
        for (auto [button, text] : {std::pair{&first, firstText}, std::pair{&second, secondText}}) {
            *button = new QToolButton(frame);
            (*button)->setText(text);
            (*button)->setCheckable(true);
            (*button)->setFocusPolicy(Qt::NoFocus);
            group->addButton(*button);
            row->addWidget(*button);
        }
        first->setChecked(true);
        return frame;
    };
    QFrame* show = segmented(m_all, tr("All Games"), m_recent, tr("Recent"), "libraryShow");
    connect(m_recent, &QToolButton::toggled, this, [this](bool on) {
        m_proxy->setRecentOnly(on);
        if (!view()->currentIndex().isValid() && m_proxy->rowCount() > 0) {
            view()->setCurrentIndex(m_proxy->index(0, 0));
        }
        updateState();
    });
    QFrame* layoutSwitch = segmented(m_gridButton, QString(), m_listButton, QString(), "libraryLayout");
    m_gridButton->setToolTip(tr("Grid"));
    m_listButton->setToolTip(tr("List"));
    connect(m_listButton, &QToolButton::toggled, this,
        [this](bool on) { setViewLayout(on ? Layout::List : Layout::Grid); });

    m_sort = new QToolButton(header);
    m_sort->setObjectName(QStringLiteral("librarySort"));
    m_sort->setPopupMode(QToolButton::InstantPopup);
    m_sort->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_sort->setLayoutDirection(Qt::RightToLeft); // the chevron after the text
    m_sort->setFocusPolicy(Qt::NoFocus);
    auto* menu = new QMenu(m_sort);
    auto* group = new QActionGroup(menu);
    const std::tuple<QString, int, Qt::SortOrder> orders[] = {
        {tr("Name"), LibraryModel::Name, Qt::AscendingOrder},
        {tr("Last Played"), LibraryModel::LastPlayed, Qt::DescendingOrder},
        {tr("Mapper"), LibraryModel::Mapper, Qt::AscendingOrder},
        {tr("Region"), LibraryModel::Region, Qt::AscendingOrder},
    };
    for (const auto& [text, column, order] : orders) {
        QAction* action = menu->addAction(text);
        action->setCheckable(true);
        action->setChecked(column == LibraryModel::Name);
        group->addAction(action);
        connect(action, &QAction::triggered, this, [this, column, order] { sortBy(column, order); });
    }
    m_sort->setMenu(menu);
    m_addFolder = new QToolButton(header);
    m_addFolder->setObjectName(QStringLiteral("libraryAddFolder"));
    m_addFolder->setText(tr("Add Folder…"));
    m_addFolder->setToolTip(tr("Add a folder of ROMs to the library"));
    m_addFolder->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_addFolder->setFocusPolicy(Qt::NoFocus);
    connect(m_addFolder, &QToolButton::clicked, this, &LibraryBrowser::addFolderRequested);
    auto* sortLabel = new QLabel(tr("Sort by"), header);
    sortLabel->setObjectName(QStringLiteral("librarySortLabel"));

    auto* top = new QHBoxLayout;
    top->setSpacing(10);
    top->addWidget(title, 0, Qt::AlignBaseline);
    top->addWidget(m_count, 0, Qt::AlignBaseline);
    top->addStretch();
    top->addWidget(m_addFolder, 0, Qt::AlignVCenter);
    top->addSpacing(4);
    top->addWidget(m_filter, 0, Qt::AlignVCenter);

    m_controls = new QWidget(header);
    auto* bottom = new QHBoxLayout(m_controls);
    bottom->setContentsMargins(0, 0, 0, 0);
    bottom->setSpacing(6);
    bottom->addWidget(show);
    bottom->addStretch();
    bottom->addWidget(sortLabel);
    bottom->addWidget(m_sort);
    bottom->addSpacing(8);
    bottom->addWidget(layoutSwitch);

    auto* column = new QVBoxLayout(header);
    column->setContentsMargins(kMargin, 20, kMargin, 8);
    column->setSpacing(14);
    column->addLayout(top);
    column->addWidget(m_controls);
    return header;
}

QWidget* LibraryBrowser::createEmptyPage()
{
    // The empty-library hint (US2-1): how to add ROMs.
    auto* page = new QWidget(this);
    m_emptyIcon = new QLabel(page);
    m_emptyIcon->setAlignment(Qt::AlignCenter);
    auto* headline = new QLabel(tr("Your library is empty"), page);
    headline->setObjectName(QStringLiteral("libraryEmptyTitle"));
    headline->setAlignment(Qt::AlignCenter);
    QFont big = headline->font();
    big.setPointSizeF(big.pointSizeF() * 1.45);
    big.setWeight(QFont::DemiBold);
    headline->setFont(big);
    m_emptyText = new QLabel(page);
    m_emptyText->setObjectName(QStringLiteral("libraryEmpty"));
    m_emptyText->setAlignment(Qt::AlignCenter);
    m_emptyText->setWordWrap(true);
    m_emptyText->setFixedWidth(460);
    m_emptyText->setTextInteractionFlags(Qt::TextSelectableByMouse);

    auto* addFolder = new QPushButton(tr("Add ROM Folder…"), page);
    addFolder->setObjectName(QStringLiteral("primaryButton"));
    auto* openFile = new QPushButton(tr("Open ROM…"), page);
    openFile->setObjectName(QStringLiteral("secondaryButton"));
    auto* change = new QPushButton(tr("Change Library Location…"), page);
    change->setObjectName(QStringLiteral("linkButton"));
    for (QPushButton* button : {addFolder, openFile, change}) button->setCursor(Qt::PointingHandCursor);
    connect(addFolder, &QPushButton::clicked, this, &LibraryBrowser::addFolderRequested);
    connect(openFile, &QPushButton::clicked, this, &LibraryBrowser::openFileRequested);
    connect(change, &QPushButton::clicked, this, &LibraryBrowser::changeLocationRequested);

    auto* buttons = new QHBoxLayout;
    buttons->setSpacing(10);
    buttons->addStretch();
    buttons->addWidget(addFolder);
    buttons->addWidget(openFile);
    buttons->addStretch();

    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(kMargin, 0, kMargin, 40);
    layout->setSpacing(0);
    layout->addStretch(2);
    layout->addWidget(m_emptyIcon);
    layout->addSpacing(22);
    layout->addWidget(headline);
    layout->addSpacing(8);
    auto* textRow = new QHBoxLayout;
    textRow->addStretch();
    textRow->addWidget(m_emptyText);
    textRow->addStretch();
    layout->addLayout(textRow);
    layout->addSpacing(22);
    layout->addLayout(buttons);
    layout->addSpacing(10);
    layout->addWidget(change, 0, Qt::AlignHCenter);
    layout->addStretch(3);
    return page;
}

void LibraryBrowser::updateStyle()
{
    // Colours come from the palette, so light and dark mode both work;
    // re-run when the palette changes.
    if (m_styling) return;
    m_styling = true;
    const QPalette palette = QWidget::palette();
    const QColor window = palette.color(QPalette::Window);
    const QColor text = palette.color(QPalette::WindowText);
    const QColor base = palette.color(QPalette::Base);
    const QColor accent = palette.color(QPalette::Active, QPalette::Highlight);
    const bool dark = isDark(palette);
    const QColor field = dark ? mix(window, text, 0.07) : base;
    const QColor border = mix(window, text, dark ? 0.16 : 0.14);
    const QColor track = mix(window, text, dark ? 0.08 : 0.07);
    const QColor thumb = dark ? mix(window, text, 0.24) : base;
    const QColor secondary = secondaryText(palette);
    const QColor accentHover = dark ? accent.lighter(115) : accent.darker(108);
    const auto c = [](const QColor& color) { return color.name(QColor::HexArgb); };

    setStyleSheet(QStringLiteral(R"(
        #libraryBrowser { background: %1; }
        QLabel#libraryCount, QLabel#librarySortLabel, QLabel#libraryEmpty, QLabel#libraryNoMatch { color: %7; }
        QLineEdit#libraryFilter {
            background: %3; border: 1px solid %4; border-radius: 9px;
            padding: 5px 6px; selection-background-color: %5; }
        QLineEdit#libraryFilter:focus { border: 2px solid %5; padding: 4px 5px; }
        QFrame[segmented="true"] { background: %6; border-radius: 8px; }
        QFrame[segmented="true"] QToolButton {
            background: transparent; border: none; border-radius: 6px;
            padding: 3px 12px; color: %2; }
        QFrame[segmented="true"] QToolButton:checked { background: %8; }
        QToolButton#librarySort { background: transparent; border: none; border-radius: 6px; padding: 3px 6px; color: %2; font-weight: 600; }
        QToolButton#librarySort:hover { background: %6; }
        QToolButton#librarySort::menu-indicator { image: none; width: 0; }
        QToolButton#libraryAddFolder { background: %6; border: none; border-radius: 8px; padding: 5px 10px; color: %2; }
        QToolButton#libraryAddFolder:hover { background: %4; }
        QListView#libraryGrid, QTreeView#libraryList { background: transparent; border: none; }
        QTreeView#libraryList { selection-background-color: %5; selection-color: %9; }
        QTreeView#libraryList::item { padding: 5px 2px; }
        QTreeView#libraryList QHeaderView::section {
            background: transparent; border: none; border-bottom: 1px solid %4;
            padding: 6px 4px; color: %7; font-weight: 600; }
        QPushButton#primaryButton {
            background: %5; color: %9; border: none; border-radius: 8px;
            padding: 8px 20px; font-weight: 600; }
        QPushButton#primaryButton:hover { background: %10; }
        QPushButton#secondaryButton {
            background: %6; color: %2; border: 1px solid %4; border-radius: 8px; padding: 7px 20px; }
        QPushButton#secondaryButton:hover { background: %4; }
        QPushButton#linkButton { background: transparent; border: none; color: %5; padding: 4px; }
        QPushButton#linkButton:hover { text-decoration: underline; }
    )")
                      .arg(c(window), c(text), c(field), c(border), c(accent), c(track), c(secondary), c(thumb),
                          c(palette.color(QPalette::Active, QPalette::HighlightedText)), c(accentHover)));

    m_filter->actions().first()->setIcon(lineIcon(QStringLiteral("search"), secondary));
    m_gridButton->setIcon(lineIcon(QStringLiteral("grid"), text));
    m_listButton->setIcon(lineIcon(QStringLiteral("list"), text));
    m_sort->setIcon(lineIcon(QStringLiteral("chevron"), text));
    m_addFolder->setIcon(lineIcon(QStringLiteral("plus"), text));
    m_emptyIcon->setPixmap(controllerPicture(palette));
    m_styling = false;
}

void LibraryBrowser::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange) {
        updateStyle();
    }
}

void LibraryBrowser::setEntries(QVector<LibraryEntry> entries)
{
    const QString selected = selectedPath();
    m_model->setEntries(std::move(entries));
    if (!selected.isEmpty()) select(selected);
    if (!view()->currentIndex().isValid() && m_proxy->rowCount() > 0) {
        view()->setCurrentIndex(m_proxy->index(0, 0));
    }
    updateState();
}

void LibraryBrowser::setLibraryRoot(const QString& root)
{
    QString text = tr("Add a folder where you keep your .nes files, or put them in the library's "
                      "roms folder. They show up here.");
    if (!root.isEmpty()) text += QStringLiteral("\n\n") + QDir::toNativeSeparators(QDir(root).filePath(QStringLiteral("roms")));
    m_emptyText->setText(text);
}

bool LibraryBrowser::select(const QString& pathOrIdentity)
{
    for (int row = 0; row < m_proxy->rowCount(); ++row) {
        const QModelIndex index = m_proxy->index(row, 0);
        if (index.data(LibraryModel::PathRole).toString() == pathOrIdentity
            || index.data(LibraryModel::IdentityRole).toString() == pathOrIdentity) {
            view()->setCurrentIndex(index);
            view()->scrollTo(index);
            return true;
        }
    }
    return false;
}

QString LibraryBrowser::selectedPath() const
{
    return view()->currentIndex().data(LibraryModel::PathRole).toString();
}

QString LibraryBrowser::filterText() const
{
    return m_filter->text();
}

void LibraryBrowser::setFilterText(const QString& text)
{
    m_filter->setText(text);
}

void LibraryBrowser::setRecentOnly(bool recentOnly)
{
    (recentOnly ? m_recent : m_all)->setChecked(true);
}

int LibraryBrowser::visibleCount() const
{
    return m_proxy->rowCount();
}

void LibraryBrowser::setViewLayout(Layout layout)
{
    m_layout = layout;
    (layout == Layout::List ? m_listButton : m_gridButton)->setChecked(true);
    const bool focused = m_grid->hasFocus() || m_list->hasFocus();
    updateState();
    if (focused) view()->setFocus();
    if (view()->currentIndex().isValid()) view()->scrollTo(view()->currentIndex());
}

QAbstractItemView* LibraryBrowser::view() const
{
    if (m_layout == Layout::List) return m_list;
    return m_grid;
}

void LibraryBrowser::activate(const QModelIndex& index)
{
    const QString path = index.data(LibraryModel::PathRole).toString();
    if (!path.isEmpty()) emit openRequested(path);
}

void LibraryBrowser::sortBy(int column, Qt::SortOrder order)
{
    m_list->sortByColumn(column, order); // sorts the shared proxy, so the grid too
}

void LibraryBrowser::updateState()
{
    const int games = m_model->rowCount();
    m_count->setText(games > 0 ? (games == 1 ? tr("1 game") : tr("%1 games").arg(games)) : QString());
    m_filter->setVisible(games > 0);
    m_addFolder->setVisible(games > 0);
    m_controls->setVisible(games > 0);
    if (games == 0) {
        m_pages->setCurrentIndex(2);
    } else if (m_proxy->rowCount() == 0) {
        m_noMatch->setText(m_filter->text().isEmpty()
                ? tr("No recent games yet.\nGames you play show up here.")
                : tr("No games match “%1”.").arg(m_filter->text()));
        m_pages->setCurrentIndex(3);
    } else {
        m_pages->setCurrentIndex(m_layout == Layout::Grid ? 0 : 1);
    }
}

void LibraryBrowser::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    m_filter->setFocus();
}

bool LibraryBrowser::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() != QEvent::KeyPress) return QWidget::eventFilter(watched, event);
    auto* key = static_cast<QKeyEvent*>(event);
    if (key->key() == Qt::Key_Escape) {
        if (!m_filter->text().isEmpty()) {
            m_filter->clear();
        } else {
            emit backRequested();
        }
        return true;
    }
    if (watched == m_filter) {
        // The arrows and Page Up/Down move through the games while typing,
        // so the keyboard never has to leave the search field.
        switch (key->key()) {
        case Qt::Key_Up:
        case Qt::Key_Down:
        case Qt::Key_PageUp:
        case Qt::Key_PageDown:
            QCoreApplication::sendEvent(view(), event);
            return true;
        default: break;
        }
    } else if (watched == m_grid || watched == m_list) {
        // Typing in the games goes to the search field.
        const QString text = key->text();
        if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
            activate(view()->currentIndex());
            return true;
        }
        if (!text.isEmpty() && text.at(0).isPrint() && !(key->modifiers() & (Qt::ControlModifier | Qt::MetaModifier))) {
            m_filter->setFocus();
            m_filter->insert(text);
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

} // namespace cutenes::app
