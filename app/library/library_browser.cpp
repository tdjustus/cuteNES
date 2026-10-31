#include "library/library_browser.hpp"

#include <QComboBox>
#include <QDir>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPushButton>
#include <QStackedWidget>
#include <QTreeView>
#include <QVBoxLayout>

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
        case Status: return e.supported ? tr("Supported") : e.reason;
        case Save: return e.hasSave ? tr("Yes") : QString();
        case States: return e.stateCount > 0 ? QString::number(e.stateCount) : QString();
        case LastPlayed:
            return e.lastPlayed.isValid()
                ? QLocale().toString(e.lastPlayed.toLocalTime(), QLocale::ShortFormat)
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

// --- LibraryBrowser -----------------------------------------------------------

LibraryBrowser::LibraryBrowser(QWidget* parent)
    : QWidget(parent),
      m_model(new LibraryModel(this)),
      m_proxy(new LibraryFilter(this)),
      m_filter(new QLineEdit(this)),
      m_show(new QComboBox(this)),
      m_view(new QTreeView(this)),
      m_pages(new QStackedWidget(this)),
      m_emptyText(new QLabel(this))
{
    setObjectName(QStringLiteral("libraryBrowser"));
    m_proxy->setSourceModel(m_model);
    m_proxy->setFilterKeyColumn(LibraryModel::Name);
    m_proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
    m_proxy->setSortRole(LibraryModel::SortRole);

    m_filter->setObjectName(QStringLiteral("libraryFilter"));
    m_filter->setPlaceholderText(tr("Type to filter"));
    m_filter->setClearButtonEnabled(true);
    m_filter->installEventFilter(this);
    connect(m_filter, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_proxy->setFilterFixedString(text);
        if (!m_view->currentIndex().isValid() && m_proxy->rowCount() > 0) {
            m_view->setCurrentIndex(m_proxy->index(0, 0));
        }
    });
    connect(m_filter, &QLineEdit::returnPressed, this, [this] { activate(m_view->currentIndex()); });

    m_show->setObjectName(QStringLiteral("libraryShow"));
    m_show->addItem(tr("All Games"));
    m_show->addItem(tr("Recent"));
    connect(m_show, &QComboBox::currentIndexChanged, this, [this](int i) { m_proxy->setRecentOnly(i == 1); });

    m_view->setObjectName(QStringLiteral("libraryView"));
    m_view->setModel(m_proxy);
    m_view->setRootIsDecorated(false);
    m_view->setUniformRowHeights(true);
    m_view->setAllColumnsShowFocus(true);
    m_view->setSortingEnabled(true);
    m_view->sortByColumn(LibraryModel::Name, Qt::AscendingOrder);
    m_view->setSelectionMode(QAbstractItemView::SingleSelection);
    m_view->header()->setStretchLastSection(false);
    m_view->header()->setSectionResizeMode(LibraryModel::Name, QHeaderView::Stretch);
    m_view->installEventFilter(this);
    connect(m_view, &QTreeView::activated, this, &LibraryBrowser::activate);

    // The empty-library hint (US2-1): how to add ROMs.
    auto* empty = new QWidget(this);
    m_emptyText->setObjectName(QStringLiteral("libraryEmpty"));
    m_emptyText->setAlignment(Qt::AlignCenter);
    m_emptyText->setWordWrap(true);
    auto* openFolder = new QPushButton(tr("Open ROM Folder"), empty);
    auto* openFile = new QPushButton(tr("Open ROM…"), empty);
    auto* change = new QPushButton(tr("Change Library Location…"), empty);
    connect(openFolder, &QPushButton::clicked, this, &LibraryBrowser::openFolderRequested);
    connect(openFile, &QPushButton::clicked, this, &LibraryBrowser::openFileRequested);
    connect(change, &QPushButton::clicked, this, &LibraryBrowser::changeLocationRequested);
    auto* buttons = new QHBoxLayout;
    buttons->addStretch();
    buttons->addWidget(openFolder);
    buttons->addWidget(openFile);
    buttons->addWidget(change);
    buttons->addStretch();
    auto* emptyLayout = new QVBoxLayout(empty);
    emptyLayout->addStretch();
    emptyLayout->addWidget(m_emptyText);
    emptyLayout->addLayout(buttons);
    emptyLayout->addStretch();

    m_pages->addWidget(m_view);
    m_pages->addWidget(empty);

    auto* bar = new QHBoxLayout;
    bar->addWidget(m_filter, 1);
    bar->addWidget(m_show);
    auto* layout = new QVBoxLayout(this);
    layout->addLayout(bar);
    layout->addWidget(m_pages, 1);
    setLibraryRoot({});
    updateEmptyState();
}

void LibraryBrowser::setEntries(QVector<LibraryEntry> entries)
{
    const QString selected = selectedPath();
    m_model->setEntries(std::move(entries));
    if (!selected.isEmpty()) select(selected);
    if (!m_view->currentIndex().isValid() && m_proxy->rowCount() > 0) {
        m_view->setCurrentIndex(m_proxy->index(0, 0));
    }
    updateEmptyState();
}

void LibraryBrowser::setLibraryRoot(const QString& root)
{
    m_emptyText->setText(tr("No ROMs yet.\n\nPut your .nes files in %1 and they appear here, "
                            "or open one from anywhere with File > Open ROM.")
            .arg(root.isEmpty() ? tr("the library's roms folder")
                                : QDir::toNativeSeparators(QDir(root).filePath(QStringLiteral("roms")))));
}

bool LibraryBrowser::select(const QString& pathOrIdentity)
{
    for (int row = 0; row < m_proxy->rowCount(); ++row) {
        const QModelIndex index = m_proxy->index(row, 0);
        if (index.data(LibraryModel::PathRole).toString() == pathOrIdentity
            || index.data(LibraryModel::IdentityRole).toString() == pathOrIdentity) {
            m_view->setCurrentIndex(index);
            m_view->scrollTo(index);
            return true;
        }
    }
    return false;
}

QString LibraryBrowser::selectedPath() const
{
    return m_view->currentIndex().data(LibraryModel::PathRole).toString();
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
    m_show->setCurrentIndex(recentOnly ? 1 : 0);
}

int LibraryBrowser::visibleCount() const
{
    return m_proxy->rowCount();
}

void LibraryBrowser::activate(const QModelIndex& index)
{
    const QString path = index.data(LibraryModel::PathRole).toString();
    if (!path.isEmpty()) emit openRequested(path);
}

void LibraryBrowser::updateEmptyState()
{
    m_pages->setCurrentIndex(m_model->rowCount() == 0 ? 1 : 0);
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
        // Up, Down, Page Up and Page Down move through the list while
        // typing, so the keyboard never has to leave the filter.
        switch (key->key()) {
        case Qt::Key_Up:
        case Qt::Key_Down:
        case Qt::Key_PageUp:
        case Qt::Key_PageDown:
            QCoreApplication::sendEvent(m_view, event);
            return true;
        default: break;
        }
    } else if (watched == m_view) {
        // Typing in the list goes to the filter.
        const QString text = key->text();
        if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
            activate(m_view->currentIndex());
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
