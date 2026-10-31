#pragma once

// The main window (spec US3, contracts/menus-and-commands.md). G0 has the
// seven menus with only File > Exit and Help > About filled in; each later
// phase adds its items.

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
