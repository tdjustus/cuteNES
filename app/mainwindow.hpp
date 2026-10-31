#pragma once

// The main window (spec US3, contracts/menus-and-commands.md). Every menu
// item of the contract is present from G0 on; an item stays disabled until
// the phase that implements it (tasks G034).

#include <QMainWindow>

class QMenu;

namespace cutenes::app {

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

    // A top-level menu by its object name: "menuFile", "menuEmulation",
    // "menuView", "menuTools", "menuDebug", "menuSettings" or "menuHelp".
    QMenu* menu(const QString& objectName) const;

public slots:
    void showAbout();

private:
    void createMenus();
};

} // namespace cutenes::app
