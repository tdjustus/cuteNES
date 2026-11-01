#include "log/app_log.hpp"

#include "about_dialog.hpp" // versionText

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QSysInfo>

#include <mutex>

namespace cutenes::app {

Q_LOGGING_CATEGORY(lcApp, "cutenes")

namespace {

struct State {
    std::mutex mutex;
    QFile file;
    QString directory;
    qint64 maxBytes = AppLog::kDefaultMaxBytes;
    bool full = false;
    QtMessageHandler previous = nullptr;
    bool installed = false;
};

State& state()
{
    static State s;
    return s;
}

const char* levelName(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg: return "debug";
    case QtInfoMsg: return "info";
    case QtWarningMsg: return "warning";
    case QtCriticalMsg: return "critical";
    case QtFatalMsg: return "fatal";
    }
    return "info";
}

// Called with the mutex held.
void writeLine(State& s, QtMsgType type, const char* category, const QString& message)
{
    if (!s.file.isOpen() || s.full) return;
    QByteArray line = QDateTime::currentDateTime().toString(Qt::ISODateWithMs).toUtf8();
    line += ' ';
    line += levelName(type);
    line += ' ';
    line += category && *category ? category : "default";
    line += ": ";
    // One message, one line: a newline inside it would read as a new entry.
    QString text = message;
    text.replace(QLatin1Char('\n'), QStringLiteral(" | "));
    line += text.toUtf8();
    line += '\n';
    if (s.file.size() + line.size() > s.maxBytes) {
        s.full = true;
        line = QDateTime::currentDateTime().toString(Qt::ISODateWithMs).toUtf8()
            + " warning cutenes: the log is full; nothing more is logged this run\n";
    }
    s.file.write(line);
    s.file.flush();
}

void handler(QtMsgType type, const QMessageLogContext& context, const QString& message)
{
    State& s = state();
    QtMessageHandler previous = nullptr;
    {
        std::lock_guard lock(s.mutex);
        writeLine(s, type, context.category, message);
        previous = s.previous;
    }
    if (previous) previous(type, context, message);
}

} // namespace

QString AppLog::defaultDirectory()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
        .filePath(QStringLiteral("logs"));
}

bool AppLog::start(const QString& directory, qint64 maxBytes)
{
    stop();
    State& s = state();
    {
        std::lock_guard lock(s.mutex);
        s.directory = directory;
        s.maxBytes = maxBytes;
        s.full = false;
        QDir().mkpath(directory);
        const QDir dir(directory);
        const QString current = dir.filePath(QStringLiteral("cutenes.log"));
        const QString previous = dir.filePath(QStringLiteral("cutenes.previous.log"));
        if (QFile::exists(current)) {
            QFile::remove(previous);
            QFile::rename(current, previous);
        }
        s.file.setFileName(current);
        if (!s.file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) return false;
        writeLine(s, QtInfoMsg, "cutenes",
            AboutDialog::versionText() + QStringLiteral(" on ") + QSysInfo::prettyProductName() + QStringLiteral(" (")
                + QSysInfo::currentCpuArchitecture() + QLatin1Char(')'));
        s.previous = qInstallMessageHandler(handler);
        s.installed = true;
    }
    return true;
}

void AppLog::stop()
{
    State& s = state();
    std::lock_guard lock(s.mutex);
    if (s.installed) {
        qInstallMessageHandler(s.previous);
        s.previous = nullptr;
        s.installed = false;
    }
    s.file.close();
}

QString AppLog::directory()
{
    std::lock_guard lock(state().mutex);
    return state().directory;
}

QString AppLog::file()
{
    std::lock_guard lock(state().mutex);
    return state().directory.isEmpty() ? QString() : QDir(state().directory).filePath(QStringLiteral("cutenes.log"));
}

} // namespace cutenes::app
