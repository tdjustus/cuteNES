#include "mainwindow.hpp"

#include "about_dialog.hpp"

#include <QAction>
#include <QApplication>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>

namespace cutenes::app {

namespace {

// 3x the NES picture at 8:7 pixel aspect (data-model "Settings": scale 3).
constexpr int kDefaultWidth = 256 * 3 * 8 / 7;
constexpr int kDefaultHeight = 240 * 3;

// Every menu action carries its gui.* command (research R11); GuiHost wires
// the property to the session from G1 on.
QAction* addCommandAction(QMenu* menu, const QString& text, const char* command)
{
    QAction* action = menu->addAction(text);
    action->setObjectName(QString::fromLatin1(command));
    action->setProperty("command", QString::fromLatin1(command));
    return action;
}

} // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("cuteNES"));
    resize(kDefaultWidth, kDefaultHeight);

    auto* placeholder = new QLabel(tr("Open a ROM to start playing."), this);
    placeholder->setAlignment(Qt::AlignCenter);
    setCentralWidget(placeholder);

    createMenus();
}

QMenu* MainWindow::menu(const QString& objectName) const
{
    return menuBar()->findChild<QMenu*>(objectName, Qt::FindDirectChildrenOnly);
}

void MainWindow::createMenus()
{
    // The order and names of spec FR-020.
    const std::pair<const char*, QString> menus[] = {
        {"menuFile", tr("&File")},
        {"menuEmulation", tr("&Emulation")},
        {"menuView", tr("&View")},
        {"menuTools", tr("&Tools")},
        {"menuDebug", tr("&Debug")},
        {"menuSettings", tr("&Settings")},
        {"menuHelp", tr("&Help")},
    };
    for (const auto& [name, title] : menus) {
        QMenu* m = menuBar()->addMenu(title);
        m->setObjectName(QString::fromLatin1(name));
    }

    QAction* exitAction = addCommandAction(menu(QStringLiteral("menuFile")), tr("E&xit"), "gui.exit");
    exitAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Q));
    exitAction->setMenuRole(QAction::QuitRole); // the application menu on macOS
    connect(exitAction, &QAction::triggered, this, &QWidget::close);

    QAction* aboutAction = addCommandAction(menu(QStringLiteral("menuHelp")), tr("&About cuteNES"),
        "gui.show_about");
    aboutAction->setMenuRole(QAction::AboutRole);
    connect(aboutAction, &QAction::triggered, this, &MainWindow::showAbout);
}

void MainWindow::showAbout()
{
    auto* dialog = new AboutDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show(); // non-modal: a command that opens a window returns at once (R11)
}

} // namespace cutenes::app
