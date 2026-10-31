#include "cli.hpp"

#include <QCommandLineParser>
#include <QDir>

namespace cutenes::app {

CommandLine parseCommandLine(const QStringList& arguments)
{
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("cuteNES, a Nintendo Entertainment System emulator"));
    const QCommandLineOption help = parser.addHelpOption();
    const QCommandLineOption version = parser.addVersionOption();
    const QCommandLineOption fullscreen(QStringLiteral("fullscreen"), QStringLiteral("Start in fullscreen."));
    parser.addOption(fullscreen);
    const QCommandLineOption library(QStringLiteral("library"),
        QStringLiteral("Use DIR as the library for this run, without changing the saved setting."),
        QStringLiteral("DIR"));
    parser.addOption(library);
    parser.addPositionalArgument(QStringLiteral("ROM"), QStringLiteral("A .nes file to play."),
        QStringLiteral("[ROM]"));

    CommandLine cli;
    if (!parser.parse(arguments)) {
        cli.exitCode = 2;
        cli.output = parser.errorText() + QStringLiteral("\n\n") + parser.helpText();
        return cli;
    }
    if (parser.isSet(help)) {
        cli.exitCode = 0;
        cli.output = parser.helpText();
        return cli;
    }
    if (parser.isSet(version)) {
        cli.exitCode = 0;
        cli.output = QStringLiteral("cuteNES " CUTENES_APP_VERSION "\n");
        return cli;
    }
    const QStringList positional = parser.positionalArguments();
    if (positional.size() > 1) {
        cli.exitCode = 2;
        cli.output = QStringLiteral("Too many arguments: give at most one ROM.\n\n") + parser.helpText();
        return cli;
    }
    cli.rom = positional.value(0);
    cli.fullscreen = parser.isSet(fullscreen);
    if (parser.isSet(library)) cli.library = QDir(parser.value(library)).absolutePath();
    return cli;
}

} // namespace cutenes::app
