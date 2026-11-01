// Help > Open Log Folder and the log behind it (contract "Help", "The log").
// - Each run writes cutenes.log in the log folder and keeps the run before
//   as cutenes.previous.log, so a bug report can include the run that went
//   wrong even after a restart.
// - A line is the time (ISO 8601, milliseconds), the level and the category,
//   then the message: Qt's own messages and the application's ("cutenes").
//   Lines from many threads stay whole.
// - A run stops at its size limit with one line saying so, so a stream of
//   warnings cannot fill the disk.
// - The window logs what it tells the user: an error or warning a menu
//   command gave. A token is never logged.
// - gui.open_log_folder shows the folder (made if missing) and returns it.

#include "gui_support.hpp"
#include "log/app_log.hpp"
#include "mainwindow.hpp"

#include <QAction>
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <thread>
#include <vector>

using cutenes::api::Json;
using cutenes::api::Result;
using cutenes::app::AppLog;
using cutenes::app::MainWindow;
using cutenes::test::waitUntil;
using cutenes::test::windowOptions;

namespace {

const QString kNestest = QStringLiteral(CUTENES_TEST_ROMS "/other/nestest.nes");

QStringList lines(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    QStringList out = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
    if (!out.isEmpty() && out.last().isEmpty()) out.removeLast();
    return out;
}

// "2026-11-01T07:30:12.345 warning default: text"
const QRegularExpression kLine(QStringLiteral(
    "^\\d{4}-\\d\\d-\\d\\dT\\d\\d:\\d\\d:\\d\\d\\.\\d{3} (debug|info|warning|critical|fatal) [\\w.]+: .*$"));

// Stops the log at the end of a test, whatever it asserted.
struct LogGuard {
    ~LogGuard() { AppLog::stop(); }
};

} // namespace

TEST(GuiAppLog, EachRunKeepsTheOneBefore)
{
    QTemporaryDir dir;
    const QString logs = QDir(dir.path()).filePath(QStringLiteral("logs")); // made by start
    LogGuard guard;

    ASSERT_TRUE(AppLog::start(logs));
    EXPECT_EQ(AppLog::file(), QDir(logs).filePath(QStringLiteral("cutenes.log")));
    qWarning("first run");
    AppLog::stop();

    ASSERT_TRUE(AppLog::start(logs));
    qWarning("second run");
    AppLog::stop();
    qWarning("after the stop"); // not logged

    const QStringList now = lines(QDir(logs).filePath(QStringLiteral("cutenes.log")));
    const QStringList before = lines(QDir(logs).filePath(QStringLiteral("cutenes.previous.log")));
    ASSERT_FALSE(now.isEmpty());
    ASSERT_FALSE(before.isEmpty());
    // Each run starts with what it is: the versions and the system.
    EXPECT_TRUE(now.first().contains(QStringLiteral("cuteNES " CUTENES_APP_VERSION))) << now.first().toStdString();
    EXPECT_TRUE(now.join(QLatin1Char('\n')).contains(QStringLiteral("warning default: second run")));
    EXPECT_FALSE(now.join(QLatin1Char('\n')).contains(QStringLiteral("first run")));
    EXPECT_TRUE(before.join(QLatin1Char('\n')).contains(QStringLiteral("warning default: first run")));
    for (const QString& line : now + before) EXPECT_TRUE(kLine.match(line).hasMatch()) << line.toStdString();
    EXPECT_FALSE((now + before).join(QLatin1Char('\n')).contains(QStringLiteral("after the stop")));

    // A third run drops the first.
    ASSERT_TRUE(AppLog::start(logs));
    AppLog::stop();
    EXPECT_FALSE(lines(QDir(logs).filePath(QStringLiteral("cutenes.previous.log"))).join(QLatin1Char('\n'))
                     .contains(QStringLiteral("first run")));
}

TEST(GuiAppLog, TheApplicationsLinesHaveItsCategoryAndThreadsDoNotMix)
{
    QTemporaryDir dir;
    LogGuard guard;
    ASSERT_TRUE(AppLog::start(dir.path()));
    qCInfo(cutenes::app::lcApp, "an application line");

    // Lines longer than a write buffer's worth together, from more threads
    // than cores, so unguarded writes would cut into each other.
    constexpr int kThreads = 16;
    constexpr int kEach = 1000;
    const QByteArray padding(400, 'x');
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([t, &padding] {
            for (int i = 0; i < kEach; ++i) {
                qWarning("thread %d line %d %s of a long enough message to interleave", t, i, padding.constData());
            }
        });
    }
    for (std::thread& t : threads) t.join();
    AppLog::stop();

    const QStringList log = lines(AppLog::file());
    EXPECT_TRUE(log.join(QLatin1Char('\n')).contains(QStringLiteral(" info cutenes: an application line")));
    int threadLines = 0;
    for (const QString& line : log) {
        EXPECT_TRUE(kLine.match(line).hasMatch()) << line.toStdString();
        if (line.contains(QStringLiteral("of a long enough message to interleave"))) ++threadLines;
    }
    EXPECT_EQ(threadLines, kThreads * kEach);
}

TEST(GuiAppLog, ARunStopsAtItsLimitWithOneLineSayingSo)
{
    QTemporaryDir dir;
    LogGuard guard;
    constexpr qint64 kLimit = 4096;
    ASSERT_TRUE(AppLog::start(dir.path(), kLimit));
    for (int i = 0; i < 1000; ++i) qWarning("line %d", i);
    AppLog::stop();

    const QStringList log = lines(AppLog::file());
    ASSERT_FALSE(log.isEmpty());
    EXPECT_TRUE(log.last().contains(QStringLiteral("the log is full"))) << log.last().toStdString();
    EXPECT_EQ(log.filter(QStringLiteral("the log is full")).size(), 1);
    EXPECT_LE(QFile(AppLog::file()).size(), kLimit + 200); // the limit, and the line saying so
    EXPECT_GT(QFile(AppLog::file()).size(), kLimit / 2);
}

TEST(GuiAppLog, TheWindowLogsTheErrorsItShowsAndNeverAToken)
{
    QTemporaryDir dir;
    LogGuard guard;
    ASSERT_TRUE(AppLog::start(dir.path()));
    const MainWindow::Options options = windowOptions();
    MainWindow w(options);
    w.show();
    w.session().call("gui.set_setting", {{"key", "emulation.pauseWhenInactive"}, {"value", false}});
    ASSERT_FALSE(w.session().call("gui.open_rom", {{"path", kNestest.toStdString()}}).isError());

    // Quick Load from the menu with an empty slot: the user is told, and so is the log.
    auto* quickLoad = w.findChild<QAction*>(QStringLiteral("gui.quick_load"));
    ASSERT_TRUE(quickLoad);
    ASSERT_TRUE(waitUntil([&] { return quickLoad->isEnabled(); }));
    quickLoad->trigger();
    EXPECT_TRUE(waitUntil([&] {
        return lines(AppLog::file()).filter(QRegularExpression(QStringLiteral("gui\\.quick_load: NotFound"))).size() == 1;
    })) << lines(AppLog::file()).join(QLatin1Char('\n')).toStdString();

    // The servers it starts, by address; the token stays in its file.
    ASSERT_FALSE(w.session().call("gui.set_setting", {{"key", "servers.apiEnabled"}, {"value", true}}).isError());
    QFile tokenFile(QDir(options.tokenDir).filePath(QStringLiteral("api.token")));
    ASSERT_TRUE(waitUntil([&] { return tokenFile.exists(); }));
    ASSERT_TRUE(tokenFile.open(QIODevice::ReadOnly));
    const Json written = Json::parse(tokenFile.readAll().toStdString());
    const QString token = QString::fromStdString(written["token"].get<std::string>());
    const QString url = QString::fromStdString(written["url"].get<std::string>());
    ASSERT_FALSE(token.isEmpty());
    ASSERT_TRUE(waitUntil([&] { return !lines(AppLog::file()).filter(QStringLiteral("API listening")).isEmpty(); }));
    w.session().call("gui.set_setting", {{"key", "servers.apiEnabled"}, {"value", false}});
    EXPECT_TRUE(waitUntil([&] { return !lines(AppLog::file()).filter(QStringLiteral("API stopped")).isEmpty(); }));

    const QString log = lines(AppLog::file()).join(QLatin1Char('\n'));
    EXPECT_TRUE(log.contains(QStringLiteral("API listening at ") + url)) << log.toStdString();
    EXPECT_TRUE(log.contains(QStringLiteral("Opened ")) && log.contains(QStringLiteral("nestest.nes"))) << log.toStdString();
    EXPECT_FALSE(log.contains(token));
}

TEST(GuiAppLog, OpenLogFolderShowsTheFolder)
{
    QTemporaryDir dir;
    MainWindow::Options options = windowOptions();
    options.logDir = QDir(dir.path()).filePath(QStringLiteral("logs"));
    MainWindow w(options);
    QString opened;
    QObject::connect(&w, &MainWindow::logFolderOpened, [&](const QString& path) { opened = path; });

    const Result r = w.session().call("gui.open_log_folder", Json::object());
    ASSERT_FALSE(r.isError()) << r.error->message;
    EXPECT_EQ(r.value["path"], QDir::toNativeSeparators(options.logDir).toStdString());
    EXPECT_TRUE(QDir(options.logDir).exists()); // made if missing
    EXPECT_TRUE(waitUntil([&] { return opened == options.logDir; })) << opened.toStdString();
}
