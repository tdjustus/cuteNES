#pragma once

// The cutenes command line (contracts/cli.md).

#include <QString>
#include <QStringList>

#include <optional>

namespace cutenes::app {

struct CommandLine {
    QString rom;
    // --library DIR: the library for this run only (absolute).
    QString library;
    bool fullscreen = false;
    // --load-state SLOT|FILE, after ROM boots: a slot (1-10) or a state
    // file (absolute). At most one is set.
    std::optional<int> loadSlot;
    QString loadFile;
    // --api, --mcp: on for this run only (--mcp sets api too). A port left
    // out is the port setting's.
    bool api = false;
    std::optional<int> apiPort;
    bool mcp = false;
    std::optional<int> mcpPort;
    // Set when the program should print `output` and exit: 0 for --help and
    // --version, 2 for a usage error.
    std::optional<int> exitCode;
    QString output;
};

// `arguments` includes the program name, as QCoreApplication::arguments().
CommandLine parseCommandLine(const QStringList& arguments);

} // namespace cutenes::app
