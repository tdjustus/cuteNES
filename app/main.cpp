// cutenes: the Qt desktop frontend (specs/003-qt-frontend). The command
// line (contracts/cli.md) arrives in G1.

#include "mainwindow.hpp"

#include <cutenes/qt/media_runtime.hpp>

#include <QApplication>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("cuteNES"));
    QApplication::setOrganizationName(QStringLiteral("cuteNES"));
    QApplication::setApplicationVersion(QStringLiteral(CUTENES_APP_VERSION));

    const cutenes::qt::MediaRuntime media;

    cutenes::app::MainWindow window;
    window.show();
    return QApplication::exec();
}
