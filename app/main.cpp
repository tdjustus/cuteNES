// cutenes: the Qt desktop frontend (specs/003-qt-frontend). The command
// line follows contracts/cli.md; options of later phases arrive with them.

#include "cli.hpp"
#include "mainwindow.hpp"

#include <cutenes/qt/media_runtime.hpp>

#include <QApplication>
#include <QMessageBox>

#include <cstdio>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("cuteNES"));
    QApplication::setOrganizationName(QStringLiteral("cuteNES"));
    QApplication::setApplicationVersion(QStringLiteral(CUTENES_APP_VERSION));

    const cutenes::app::CommandLine cli = cutenes::app::parseCommandLine(QApplication::arguments());
    if (cli.exitCode) {
        const QByteArray text = cli.output.toLocal8Bit();
        std::fputs(text.constData(), *cli.exitCode == 0 ? stdout : stderr);
#if defined(_WIN32)
        // A GUI program has no console on Windows (contract "Errors").
        QMessageBox::information(nullptr, QStringLiteral("cuteNES"), cli.output);
#endif
        return *cli.exitCode;
    }

    const cutenes::qt::MediaRuntime media;

    cutenes::app::MainWindow::Options options;
    options.libraryRoot = cli.library;
    cutenes::app::MainWindow window(options);
    if (cli.fullscreen) {
        window.showFullScreen();
    } else {
        window.show();
    }
    window.checkLibrary();
    if (!cli.rom.isEmpty()) window.openRom(cli.rom);
    return QApplication::exec();
}
