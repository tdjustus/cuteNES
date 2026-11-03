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
    const QCommandLineOption loadState(QStringLiteral("load-state"),
        QStringLiteral("After booting ROM, load slot 1-10 or a state file."), QStringLiteral("SLOT|FILE"));
    parser.addOption(loadState);
    const QCommandLineOption api(QStringLiteral("api"), QStringLiteral("Turn the API server on for this run."));
    parser.addOption(api);
    const QCommandLineOption apiPort(QStringLiteral("api-port"),
        QStringLiteral("With --api or --mcp: the API listens on port N for this run (0 picks a free one)."), QStringLiteral("N"));
    parser.addOption(apiPort);
    const QCommandLineOption mcp(QStringLiteral("mcp"),
        QStringLiteral("Turn the MCP server, and the API it needs, on for this run."));
    parser.addOption(mcp);
    const QCommandLineOption mcpPort(QStringLiteral("mcp-port"),
        QStringLiteral("With --mcp: the MCP server listens on port N for this run (0 picks a free one)."), QStringLiteral("N"));
    parser.addOption(mcpPort);
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
    const auto usageError = [&](const QString& reason) {
        cli.exitCode = 2;
        cli.output = reason + QStringLiteral("\n\n") + parser.helpText();
        return cli;
    };
    const QStringList positional = parser.positionalArguments();
    if (positional.size() > 1) return usageError(QStringLiteral("Too many arguments: give at most one ROM."));
    cli.rom = positional.value(0);
    cli.fullscreen = parser.isSet(fullscreen);
    if (parser.isSet(library)) cli.library = QDir(parser.value(library)).absolutePath();

    if (parser.isSet(loadState)) {
        if (cli.rom.isEmpty()) return usageError(QStringLiteral("--load-state needs a ROM to boot first."));
        const QString value = parser.value(loadState);
        // All digits: a slot. A state file named only by digits takes a
        // path such as ./3.
        bool number = !value.isEmpty();
        for (const QChar c : value) number = number && c.isDigit();
        if (number) {
            const int slot = value.toInt();
            if (slot < 1 || slot > 10) return usageError(QStringLiteral("--load-state: slots are 1 to 10."));
            cli.loadSlot = slot;
        } else {
            cli.loadFile = QDir(value).absolutePath();
        }
    }

    cli.mcp = parser.isSet(mcp);
    cli.api = parser.isSet(api) || cli.mcp; // MCP needs the API
    // The synopsis nests each port in its server's option (--mcp turns
    // the API on, so it takes --api-port too).
    const auto port = [&](const QCommandLineOption& option, bool serverOn, const char* server,
                          std::optional<int>& out) -> bool {
        if (!parser.isSet(option)) return true;
        const QString name = option.names().constFirst();
        if (!serverOn) {
            usageError(QStringLiteral("--%1 needs --%2.").arg(name, QLatin1String(server)));
            return false;
        }
        bool ok = false;
        const int n = parser.value(option).toInt(&ok);
        if (!ok || n < 0 || n > 65535) {
            usageError(QStringLiteral("--%1: give a port from 0 to 65535 (0 picks a free one).").arg(name));
            return false;
        }
        out = n;
        return true;
    };
    if (!port(apiPort, cli.api, "api", cli.apiPort)) return cli;
    if (!port(mcpPort, cli.mcp, "mcp", cli.mcpPort)) return cli;
    return cli;
}

} // namespace cutenes::app
