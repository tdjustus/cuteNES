#pragma once

// The cutenes command line (contracts/cli.md).

#include <QString>
#include <QStringList>

#include <optional>

namespace cutenes::app {

struct CommandLine {
    QString rom;
    bool fullscreen = false;
    // Set when the program should print `output` and exit: 0 for --help and
    // --version, 2 for a usage error.
    std::optional<int> exitCode;
    QString output;
};

// `arguments` includes the program name, as QCoreApplication::arguments().
CommandLine parseCommandLine(const QStringList& arguments);

} // namespace cutenes::app
