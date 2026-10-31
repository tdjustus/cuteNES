#pragma once

// The library browser (spec US1, FR-002, FR-003): the ROMs of the library
// and recent games, with name, mapper, region, support, save and state
// flags and last played. Type to filter, sort by any column, show only
// recent games, and launch with Enter or a double click. Fully usable from
// the keyboard; gamepad focus arrives in G4.

#include "library/library_index.hpp"

#include <QAbstractTableModel>
#include <QSortFilterProxyModel>
#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class QStackedWidget;
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

    QTreeView* view() const { return m_view; }
    QLineEdit* filterEdit() const { return m_filter; }

signals:
    void openRequested(const QString& path);
    void openFolderRequested();
    void openFileRequested();
    void changeLocationRequested();
    // Escape while a game is loaded.
    void backRequested();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    void activate(const QModelIndex& index);
    void updateEmptyState();

    LibraryModel* m_model;
    LibraryFilter* m_proxy;
    QLineEdit* m_filter;
    QComboBox* m_show;
    QTreeView* m_view;
    QStackedWidget* m_pages;
    QLabel* m_emptyText;
};

} // namespace cutenes::app
