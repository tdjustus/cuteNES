#pragma once

// Settings (contracts/menus-and-commands.md "Settings"): one page per
// group. G2 fills the Library page; the others arrive with their phases.

#include <QDialog>

class QLabel;
class QListWidget;
class QStackedWidget;

namespace cutenes::app {

class SettingsDialog final : public QDialog {
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget* parent = nullptr);

    // "video", "audio", "input", "hotkeys", "emulation", "library" or "servers".
    void showPage(const QString& page);
    QString currentPage() const;
    void setLibraryRoot(const QString& root);

signals:
    void pageShown(const QString& page);
    void changeLibraryRequested();
    void openLibraryFolderRequested();

private:
    QListWidget* m_pages;
    QStackedWidget* m_stack;
    QLabel* m_libraryRoot;
};

} // namespace cutenes::app
