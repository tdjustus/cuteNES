#pragma once

// The library browser (spec US1, FR-002, FR-003): the ROMs of the library
// and recent games, with name, mapper, region, support, save and state
// flags and last played. Games show as cards in a grid, or as rows in a
// list. Type to filter, sort, show only recent games, and launch with Enter
// or a double click. Fully usable from the keyboard; gamepad focus arrives
// in G4.

#include "library/library_index.hpp"

#include <QAbstractTableModel>
#include <QSortFilterProxyModel>
#include <QWidget>

class QAbstractItemView;
class QLabel;
class QLineEdit;
class QListView;
class QStackedWidget;
class QToolButton;
class QTreeView;

namespace cutenes::app {

class LibraryModel final : public QAbstractTableModel {
    Q_OBJECT

public:
    enum Column { Name, Mapper, Region, Status, Save, States, LastPlayed, ColumnCount };
    enum Role { PathRole = Qt::UserRole, IdentityRole, RecentRole, SortRole };

    using QAbstractTableModel::QAbstractTableModel;

    void setEntries(QVector<LibraryEntry> entries);
    const QVector<LibraryEntry>& entries() const { return m_entries; }

    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

private:
    QVector<LibraryEntry> m_entries;
};

class LibraryFilter final : public QSortFilterProxyModel {
    Q_OBJECT

public:
    using QSortFilterProxyModel::QSortFilterProxyModel;
    void setRecentOnly(bool recentOnly);
    bool recentOnly() const { return m_recentOnly; }

protected:
    bool filterAcceptsRow(int row, const QModelIndex& parent) const override;

private:
    bool m_recentOnly = false;
};

class LibraryBrowser final : public QWidget {
    Q_OBJECT

public:
    enum class Layout { Grid, List };

    explicit LibraryBrowser(QWidget* parent = nullptr);

    void setEntries(QVector<LibraryEntry> entries);
    // Shows where the library is, for the empty-library hint.
    void setLibraryRoot(const QString& root);

    // Selects the entry with this path or identity; false if none.
    bool select(const QString& pathOrIdentity);
    QString selectedPath() const;
    QString filterText() const;
    void setFilterText(const QString& text);
    void setRecentOnly(bool recentOnly);
    int visibleCount() const;
    Layout viewLayout() const { return m_layout; }
    void setViewLayout(Layout layout);

    // The view showing the games (the grid or the list).
    QAbstractItemView* view() const;
    QLineEdit* filterEdit() const { return m_filter; }

signals:
    void openRequested(const QString& path);
    void addFolderRequested();
    void openFileRequested();
    void changeLocationRequested();
    // Escape while a game is loaded.
    void backRequested();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    QWidget* createHeader();
    QWidget* createEmptyPage();
    void activate(const QModelIndex& index);
    void sortBy(int column, Qt::SortOrder order);
    void updateState();
    void updateStyle();

    LibraryModel* m_model;
    LibraryFilter* m_proxy;
    QLineEdit* m_filter = nullptr;
    QLabel* m_count = nullptr;
    QToolButton* m_all = nullptr;
    QToolButton* m_recent = nullptr;
    QToolButton* m_gridButton = nullptr;
    QToolButton* m_listButton = nullptr;
    QToolButton* m_sort = nullptr;
    QToolButton* m_addFolder = nullptr;
    QWidget* m_controls = nullptr;
    QLabel* m_emptyIcon = nullptr;
    Layout m_layout = Layout::Grid;
    bool m_styling = false;
    QListView* m_grid = nullptr;
    QTreeView* m_list = nullptr;
    QStackedWidget* m_pages = nullptr;
    QLabel* m_emptyText = nullptr;
    QLabel* m_noMatch = nullptr;
};

} // namespace cutenes::app
