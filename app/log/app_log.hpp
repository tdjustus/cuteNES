#pragma once

// The application's log (contract menus-and-commands.md "The log"), which
// Help > Open Log Folder shows. A bug report needs what the program saw
// when it went wrong, and a GUI program has no console to read it from.
//
// - Each run writes cutenes.log in the log folder and keeps the run before
//   as cutenes.previous.log, so the run that went wrong survives the
//   restart that follows it.
// - The log takes every Qt message (qDebug ... qFatal, Qt's own included)
//   through a message handler, and passes each on to the handler before it,
//   so the messages still reach the terminal or debugger.
// - A line is "<time> <level> <category>: <message>", the time in ISO 8601
//   local time with milliseconds. A mutex keeps lines from many threads
//   whole, and each is flushed, so a crash loses nothing written before it.
// - A run stops at maxBytes (10 MB by default) with one line saying so: a
//   warning repeated every frame cannot fill the disk.
// - The application's own lines use the category "cutenes" (lcApp): what it
//   told the user, games opened and closed, servers started and stopped.
//   Nothing secret goes in (no tokens); paths do, so the folder is the
//   user's to share.
//
// Not NES-specific: any Qt application can use it as it is.

#include <QLoggingCategory>
#include <QString>

namespace cutenes::app {

Q_DECLARE_LOGGING_CATEGORY(lcApp)

class AppLog {
public:
    static constexpr qint64 kDefaultMaxBytes = 10 * 1024 * 1024;

    // <AppLocalData>/logs: beside the debug folder, outside the library.
    static QString defaultDirectory();

    // Starts logging to <directory>/cutenes.log (making the folder), after
    // moving an existing one to cutenes.previous.log. False if the file
    // cannot be opened; messages then go only where they went before.
    static bool start(const QString& directory, qint64 maxBytes = kDefaultMaxBytes);
    // Puts the previous handler back and closes the file.
    static void stop();

    // The folder and file of the log started last ("" before any start).
    static QString directory();
    static QString file();
};

} // namespace cutenes::app
