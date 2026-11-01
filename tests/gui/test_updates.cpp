// G097/G098 (spec FR-090 to FR-093, FR-096, FR-097; contract "Updates";
// research R16): the update check, the first-launch question and the prompt,
// against a fake release feed on loopback. Expectations come from the spec
// and contract:
// - with updates.check off there is no request at all;
// - the first launch asks once; either answer is kept; a ROM on the command
//   line puts the question off, and means no check that launch;
// - with checks on, one request per launch, carrying only cuteNES's
//   User-Agent; a newer release prompts with both versions;
// - Skip This Version is kept and a newer version still prompts; Ask Again
//   Later keeps nothing; See Details opens the release page;
// - a game running when the answer arrives means no prompt; a failed check
//   is logged and shown nowhere else;
// - a build that cannot replace itself offers no Install and says why;
// - the three settings are readable and settable; no command checks,
//   prompts or installs.

#include "gui_support.hpp"
#include "log/app_log.hpp"
#include "mainwindow.hpp"

#include <QAbstractButton>
#include <QCheckBox>
#include <QDialog>
#include <QDir>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QFile>
#include <QProgressDialog>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTemporaryDir>

#include <httplib.h>

#include <gtest/gtest.h>

#include <atomic>
#include <map>
#include <chrono>
#include <mutex>
#include <thread>

using cutenes::api::Json;
using cutenes::api::Result;
using cutenes::app::AppLog;
using cutenes::app::MainWindow;
using cutenes::test::waitUntil;
using cutenes::test::windowOptions;

namespace {

const QString kNestest = QStringLiteral(CUTENES_TEST_ROMS "/other/nestest.nes");

// A release list in GitHub's form with one release for every platform,
// its files at `base` (GitHub's address, or the fake feed's own).
std::string feedWith(const std::string& version,
    const std::string& base = "https://github.com/tdjustus/cuteNES/releases/download")
{
    Json assets = Json::array();
    for (const char* suffix : {"macos-arm64.zip", "macos-x86_64.zip", "windows-x64.zip", "linux-x86_64.AppImage"}) {
        const std::string name = "cuteNES-" + version + "-" + suffix;
        assets.push_back({{"name", name}, {"browser_download_url", base + "/v" + version + "/" + name}});
    }
    assets.push_back({{"name", "SHA256SUMS"}, {"browser_download_url", base + "/v" + version + "/SHA256SUMS"}});
    return Json::array({{{"tag_name", "v" + version}, {"draft", false}, {"prerelease", false},
                           {"html_url", "https://github.com/tdjustus/cuteNES/releases/tag/v" + version},
                           {"body", "What is new in " + version}, {"assets", assets}}})
        .dump();
}

// The fake feed: answers GET /releases with `body` and `status` after
// `delay`, and keeps each request's headers. It also serves release files
// (GET /download/v<version>/<name>), each after `fileDelay`.
class FakeFeed {
public:
    FakeFeed()
    {
        m_server.Get(R"(/download/[^/]+/(.+))", [this](const httplib::Request& request, httplib::Response& response) {
            std::this_thread::sleep_for(m_fileDelay.load());
            std::lock_guard lock(m_mutex);
            const auto it = m_files.find(request.matches[1]);
            if (it == m_files.end()) {
                response.status = 404;
                return;
            }
            response.set_content(it->second, "application/octet-stream");
        });
        m_server.Get("/releases", [this](const httplib::Request& request, httplib::Response& response) {
            std::this_thread::sleep_for(m_delay.load());
            std::lock_guard lock(m_mutex);
            m_requests.push_back(request);
            response.status = m_status;
            response.set_content(m_body, "application/json");
        });
        m_port = m_server.bind_to_any_port("127.0.0.1");
        m_thread = std::thread([this] { m_server.listen_after_bind(); });
        m_server.wait_until_ready();
    }
    ~FakeFeed()
    {
        m_server.stop();
        m_thread.join();
    }

    QUrl url() const { return QUrl(QStringLiteral("http://127.0.0.1:%1/releases?per_page=30").arg(m_port)); }
    std::string files() const { return "http://127.0.0.1:" + std::to_string(m_port) + "/download"; }
    void file(const std::string& name, const std::string& bytes)
    {
        std::lock_guard lock(m_mutex);
        m_files[name] = bytes;
    }
    void fileDelay(std::chrono::milliseconds d) { m_fileDelay = d; }
    void serve(const std::string& body, int status = 200)
    {
        std::lock_guard lock(m_mutex);
        m_body = body;
        m_status = status;
    }
    void delay(std::chrono::milliseconds d) { m_delay = d; }
    std::vector<httplib::Request> requests()
    {
        std::lock_guard lock(m_mutex);
        return m_requests;
    }

private:
    httplib::Server m_server;
    std::thread m_thread;
    int m_port = 0;
    std::mutex m_mutex;
    std::string m_body = feedWith("1.2.0");
    int m_status = 200;
    std::atomic<std::chrono::milliseconds> m_delay{std::chrono::milliseconds(0)};
    std::atomic<std::chrono::milliseconds> m_fileDelay{std::chrono::milliseconds(0)};
    std::map<std::string, std::string> m_files;
    std::vector<httplib::Request> m_requests;
};

Json call(MainWindow& w, const char* name, const Json& args = Json::object())
{
    const Result r = w.session().call(name, args);
    EXPECT_FALSE(r.isError()) << name << ": " << (r.isError() ? r.error->message : "");
    return r.value;
}

Json updates(MainWindow& w)
{
    return call(w, "gui.get_settings")["values"]["updates"];
}

// Options for a window reading `feed`, sharing `settings` across windows
// (one launch each).
MainWindow::Options feedOptions(const FakeFeed& feed, const QString& settings, bool ask)
{
    MainWindow::Options options = windowOptions();
    options.settingsFile = settings;
    options.updateFeed = feed.url();
    options.allowLoopbackFeed = true;
    options.askAboutUpdates = ask;
    return options;
}

QDialog* shown(MainWindow& w, const char* name)
{
    QDialog* found = nullptr;
    waitUntil([&] {
        found = w.findChild<QDialog*>(QString::fromLatin1(name));
        return found && found->isVisible();
    });
    return found && found->isVisible() ? found : nullptr;
}

QAbstractButton* button(QWidget* parent, const char* name)
{
    return parent->findChild<QAbstractButton*>(QString::fromLatin1(name));
}

// Nothing happens for a while: the request would have come and gone.
void settle(int ms = 700)
{
    waitUntil([] { return false; }, ms);
}

} // namespace

TEST(GuiUpdates, WithChecksOffThereIsNoRequest)
{
    FakeFeed feed;
    QTemporaryDir dir;
    MainWindow w(feedOptions(feed, dir.filePath(QStringLiteral("cutenes.ini")), false));
    w.show();
    EXPECT_EQ(updates(w), (Json{{"check", false}, {"asked", false}, {"skippedVersion", ""}}));
    w.startUpdates(false);
    settle();
    EXPECT_TRUE(feed.requests().empty());
    EXPECT_EQ(shown(w, "updatePrompt"), nullptr);
}

TEST(GuiUpdates, TheFirstLaunchAsksOnceAndKeepsTheAnswer)
{
    FakeFeed feed;
    QTemporaryDir dir;
    const QString settings = dir.filePath(QStringLiteral("cutenes.ini"));
    {
        // A ROM on the command line: the question waits, and nothing is fetched.
        MainWindow w(feedOptions(feed, settings, true));
        w.show();
        w.startUpdates(true);
        settle();
        EXPECT_EQ(shown(w, "updatesQuestion"), nullptr);
        EXPECT_EQ(updates(w)["asked"], false);
    }
    {
        MainWindow w(feedOptions(feed, settings, true));
        w.show();
        w.startUpdates(false);
        auto* question = qobject_cast<QMessageBox*>(shown(w, "updatesQuestion"));
        ASSERT_TRUE(question);
        EXPECT_TRUE(question->text().contains(QStringLiteral("updates"))) << question->text().toStdString();
        question->button(QMessageBox::No)->click();
        EXPECT_TRUE(waitUntil([&] { return updates(w)["asked"] == true; }));
        EXPECT_EQ(updates(w)["check"], false);
        settle();
        EXPECT_TRUE(feed.requests().empty());
    }
    {
        // Asked: never again.
        MainWindow w(feedOptions(feed, settings, true));
        w.show();
        w.startUpdates(false);
        settle();
        EXPECT_EQ(shown(w, "updatesQuestion"), nullptr);
    }
}

TEST(GuiUpdates, YesTurnsChecksOnAndChecksAtOnce)
{
    FakeFeed feed;
    QTemporaryDir dir;
    MainWindow w(feedOptions(feed, dir.filePath(QStringLiteral("cutenes.ini")), true));
    w.show();
    w.startUpdates(false);
    auto* question = qobject_cast<QMessageBox*>(shown(w, "updatesQuestion"));
    ASSERT_TRUE(question);
    question->button(QMessageBox::Yes)->click();
    EXPECT_TRUE(waitUntil([&] { return updates(w)["check"] == true && updates(w)["asked"] == true; }));
    EXPECT_NE(shown(w, "updatePrompt"), nullptr);
    EXPECT_EQ(feed.requests().size(), 1u);
}

TEST(GuiUpdates, ANewerReleasePromptsOnceWithBothVersions)
{
    FakeFeed feed;
    QTemporaryDir dir;
    MainWindow w(feedOptions(feed, dir.filePath(QStringLiteral("cutenes.ini")), false));
    w.show();
    call(w, "gui.set_setting", {{"key", "updates.check"}, {"value", true}});
    w.startUpdates(false);
    QDialog* prompt = shown(w, "updatePrompt");
    ASSERT_TRUE(prompt);

    // One request, saying only who is asking.
    const auto requests = feed.requests();
    ASSERT_EQ(requests.size(), 1u);
    EXPECT_EQ(requests[0].get_header_value("User-Agent"), "cuteNES/" CUTENES_APP_VERSION);
    EXPECT_EQ(requests[0].get_header_value("Accept"), "application/vnd.github+json");
    EXPECT_EQ(requests[0].get_param_value("per_page"), "30");
    EXPECT_FALSE(requests[0].has_header("Cookie"));
    EXPECT_FALSE(requests[0].has_header("Authorization"));

    auto* version = prompt->findChild<QLabel*>(QStringLiteral("updateVersion"));
    ASSERT_TRUE(version);
    EXPECT_TRUE(version->text().contains(QStringLiteral("1.2.0"))) << version->text().toStdString();
    EXPECT_TRUE(version->text().contains(QStringLiteral(CUTENES_APP_VERSION))) << version->text().toStdString();
    EXPECT_TRUE(prompt->findChild<QWidget*>(QStringLiteral("updateNotes")));
    for (const char* name : {"updateLater", "updateSkip", "updateDetails"}) EXPECT_TRUE(button(prompt, name)) << name;

    // This build is not a release package (CUTENES_PACKAGE none): no
    // Install, and the prompt says why (FR-096).
    EXPECT_EQ(button(prompt, "updateInstall"), nullptr);
    auto* why = prompt->findChild<QLabel*>(QStringLiteral("updateWhyNoInstall"));
    ASSERT_TRUE(why);
    EXPECT_FALSE(why->text().isEmpty());
}

TEST(GuiUpdates, SkipIsKeptPerVersionAndLaterKeepsNothing)
{
    FakeFeed feed;
    QTemporaryDir dir;
    const QString settings = dir.filePath(QStringLiteral("cutenes.ini"));
    const auto launch = [&](const char* press, bool expectPrompt) {
        MainWindow w(feedOptions(feed, settings, false));
        w.show();
        call(w, "gui.set_setting", {{"key", "updates.check"}, {"value", true}});
        const std::size_t before = feed.requests().size();
        QUrl opened;
        QObject::connect(&w, &MainWindow::externalUrlOpened, [&](const QUrl& url) { opened = url; });
        w.startUpdates(false);
        EXPECT_TRUE(waitUntil([&] { return feed.requests().size() == before + 1; }));
        QDialog* prompt = shown(w, "updatePrompt");
        EXPECT_EQ(prompt != nullptr, expectPrompt) << press;
        if (prompt && press) {
            button(prompt, press)->click();
            EXPECT_TRUE(waitUntil([&] { return !prompt->isVisible(); })) << press;
            if (QLatin1String(press) == QLatin1String("updateDetails")) {
                EXPECT_TRUE(waitUntil([&] { return !opened.isEmpty(); }));
                EXPECT_EQ(opened, QUrl(QStringLiteral("https://github.com/tdjustus/cuteNES/releases/tag/v1.2.0")));
            }
        }
        return updates(w)["skippedVersion"];
    };

    EXPECT_EQ(launch("updateLater", true), "");    // later: nothing kept
    EXPECT_EQ(launch("updateDetails", true), "");  // details: the page, nothing kept
    EXPECT_EQ(launch("updateSkip", true), "1.2.0");
    EXPECT_EQ(launch(nullptr, false), "1.2.0");    // skipped: checked, not offered
    feed.serve(feedWith("1.3.0"));
    EXPECT_EQ(launch(nullptr, true), "1.2.0");     // a newer one is
}

TEST(GuiUpdates, AGameRunningWhenTheAnswerComesMeansNoPrompt)
{
    FakeFeed feed;
    feed.delay(std::chrono::milliseconds(800));
    QTemporaryDir dir;
    MainWindow w(feedOptions(feed, dir.filePath(QStringLiteral("cutenes.ini")), false));
    w.show();
    call(w, "gui.set_setting", {{"key", "updates.check"}, {"value", true}});
    call(w, "gui.set_setting", {{"key", "emulation.pauseWhenInactive"}, {"value", false}});
    w.startUpdates(false);
    call(w, "gui.open_rom", {{"path", kNestest.toStdString()}});
    EXPECT_TRUE(waitUntil([&] { return feed.requests().size() == 1u; }, 5000));
    settle();
    EXPECT_EQ(shown(w, "updatePrompt"), nullptr);
}

TEST(GuiUpdates, ARomOnTheCommandLineMeansNoCheck)
{
    FakeFeed feed;
    QTemporaryDir dir;
    MainWindow w(feedOptions(feed, dir.filePath(QStringLiteral("cutenes.ini")), false));
    w.show();
    call(w, "gui.set_setting", {{"key", "updates.check"}, {"value", true}});
    w.startUpdates(true);
    settle();
    EXPECT_TRUE(feed.requests().empty());
}

TEST(GuiUpdates, AFailedCheckIsLoggedAndShownNowhereElse)
{
    FakeFeed feed;
    feed.serve(R"({"message":"API rate limit exceeded for 127.0.0.1."})", 403);
    QTemporaryDir dir;
    ASSERT_TRUE(AppLog::start(dir.path()));
    {
        MainWindow w(feedOptions(feed, dir.filePath(QStringLiteral("cutenes.ini")), false));
        w.show();
        call(w, "gui.set_setting", {{"key", "updates.check"}, {"value", true}});
        w.startUpdates(false);
        EXPECT_TRUE(waitUntil([&] { return feed.requests().size() == 1u; }));
        settle();
        EXPECT_EQ(shown(w, "updatePrompt"), nullptr);
        EXPECT_EQ(w.findChild<QMessageBox*>(QStringLiteral("warning")), nullptr);
    }
    AppLog::stop();
    QFile log(AppLog::file());
    ASSERT_TRUE(log.open(QIODevice::ReadOnly));
    const QString text = QString::fromUtf8(log.readAll());
    EXPECT_TRUE(text.contains(QStringLiteral("rate limit"))) << text.toStdString();
}

TEST(GuiUpdates, TheSettingsAreReadAndSetButNoCommandUpdates)
{
    MainWindow w(windowOptions());
    const Json schema = call(w, "gui.get_settings")["schema"];
    for (const char* key : {"updates.check", "updates.asked", "updates.skippedVersion"}) {
        EXPECT_TRUE(schema.contains(key)) << key;
    }
    call(w, "gui.set_setting", {{"key", "updates.skippedVersion"}, {"value", "1.4.2"}});
    call(w, "gui.set_setting", {{"key", "updates.asked"}, {"value", true}});
    EXPECT_EQ(updates(w), (Json{{"check", false}, {"asked", true}, {"skippedVersion", "1.4.2"}}));
    call(w, "gui.set_setting", {{"key", "updates.skippedVersion"}, {"value", ""}});
    EXPECT_EQ(updates(w)["skippedVersion"], "");
    for (const Json& bad : {Json("1.4"), Json("v1.4.2-rc1"), Json(3)}) {
        const Result r = w.session().call("gui.set_setting", {{"key", "updates.skippedVersion"}, {"value", bad}});
        ASSERT_TRUE(r.isError()) << bad.dump();
        EXPECT_EQ(r.error->name, "InvalidArguments");
    }
    EXPECT_TRUE(w.session().call("gui.set_setting", {{"key", "updates.check"}, {"value", "yes"}}).isError());

    // FR-097: checking, prompting and installing follow from the setting
    // and the user's click, never from a command.
    const Result catalog = w.session().call("api.catalog", Json::object());
    ASSERT_FALSE(catalog.isError()) << catalog.error->message;
    for (const Json& command : catalog.value["commands"]) {
        const std::string name = command["name"];
        EXPECT_EQ(name.find("update"), std::string::npos) << name;
    }
}

TEST(GuiUpdates, TheUpdatesPageShowsAndChangesTheSettings)
{
    MainWindow w(windowOptions());
    w.show();
    call(w, "gui.set_setting", {{"key", "updates.skippedVersion"}, {"value", "1.4.2"}});
    call(w, "gui.show_settings", {{"page", "updates"}});
    QDialog* dialog = shown(w, "settingsDialog");
    ASSERT_TRUE(dialog);
    auto* check = dialog->findChild<QCheckBox*>(QStringLiteral("updatesCheck"));
    auto* skipped = dialog->findChild<QLabel*>(QStringLiteral("updatesSkipped"));
    auto* forget = dialog->findChild<QPushButton*>(QStringLiteral("updatesForgetSkipped"));
    auto* install = dialog->findChild<QLabel*>(QStringLiteral("updatesInstall"));
    ASSERT_TRUE(check && skipped && forget && install);
    EXPECT_FALSE(check->isChecked());
    EXPECT_TRUE(waitUntil([&] { return skipped->text().contains(QStringLiteral("1.4.2")); }))
        << skipped->text().toStdString();
    EXPECT_FALSE(install->text().isEmpty());

    check->click();
    EXPECT_TRUE(waitUntil([&] { return updates(w)["check"] == true; }));
    EXPECT_TRUE(waitUntil([&] { return forget->isEnabled(); }));
    forget->click();
    EXPECT_TRUE(waitUntil([&] { return updates(w)["skippedVersion"] == ""; }));
    EXPECT_TRUE(waitUntil([&] { return !forget->isEnabled(); }));
}

namespace {

// A release package's install, an AppImage in a temporary folder: the swap
// is the same on every system, so the whole Install path runs everywhere.
struct FakeInstall {
    QTemporaryDir dir;
    QString image = dir.filePath(QStringLiteral("cuteNES.AppImage"));
    FakeInstall()
    {
        QFile file(image);
        EXPECT_TRUE(file.open(QIODevice::WriteOnly));
        file.write("the installed version");
    }
    QByteArray contents() const
    {
        QFile file(image);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }
    QStringList entries() const { return QDir(dir.path()).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot); }
};

const std::string kImage = "cuteNES-1.2.0-linux-x86_64.AppImage";

// Serves 1.2.0 with `build` as the AppImage and `sums` as SHA256SUMS
// (empty: the build's real checksum).
void release(FakeFeed& feed, const std::string& build, std::string sums = {})
{
    feed.serve(feedWith("1.2.0", feed.files()));
    feed.file(kImage, build);
    if (sums.empty()) {
        sums = QCryptographicHash::hash(QByteArray::fromStdString(build), QCryptographicHash::Sha256).toHex().toStdString()
            + "  " + kImage + "\n";
    }
    feed.file("SHA256SUMS", sums);
}

MainWindow::Options installOptions(const FakeFeed& feed, const QString& settings, const FakeInstall& install)
{
    MainWindow::Options options = feedOptions(feed, settings, false);
    options.updateTarget = cutenes::app::update::Target{QStringLiteral("appimage"), install.image};
    options.relaunchAfterUpdate = false;
    options.arguments = QStringList{QStringLiteral("--mcp"), QStringLiteral("--library"),
        QStringLiteral("/tmp/a library"), QStringLiteral("--fullscreen")};
    return options;
}

QDialog* promptWithChecksOn(MainWindow& w)
{
    w.show();
    call(w, "gui.set_setting", {{"key", "updates.check"}, {"value", true}});
    w.startUpdates(false);
    return shown(w, "updatePrompt");
}

} // namespace

TEST(GuiUpdates, InstallReplacesTheBuildAndRelaunchesWithTheSameArguments)
{
    FakeFeed feed;
    release(feed, "the new version");
    FakeInstall install;
    QTemporaryDir dir;
    MainWindow w(installOptions(feed, dir.filePath(QStringLiteral("cutenes.ini")), install));
    QString exe;
    QStringList arguments;
    bool installed = false;
    QObject::connect(&w, &MainWindow::updateInstalled, [&](const QString& program, const QStringList& args) {
        exe = program;
        arguments = args;
        installed = true;
    });
    QDialog* prompt = promptWithChecksOn(w);
    ASSERT_TRUE(prompt);
    auto* installButton = button(prompt, "updateInstall");
    ASSERT_TRUE(installButton) << "a release package with write access offers Install";
    EXPECT_EQ(prompt->findChild<QLabel*>(QStringLiteral("updateWhyNoInstall")), nullptr);
    installButton->click();

    ASSERT_TRUE(waitUntil([&] { return installed; }, 10000));
    EXPECT_EQ(install.contents(), "the new version");
    EXPECT_EQ(exe, install.image);
    EXPECT_EQ(arguments, (QStringList{QStringLiteral("--mcp"), QStringLiteral("--library"),
                             QStringLiteral("/tmp/a library"), QStringLiteral("--fullscreen")}));
    EXPECT_EQ(install.entries(), QStringList{QStringLiteral("cuteNES.AppImage")}); // no download left
#if !defined(_WIN32)
    EXPECT_TRUE(QFileInfo(install.image).isExecutable());
#endif
}

TEST(GuiUpdates, ABadChecksumReplacesNothingAndSaysSo)
{
    FakeFeed feed;
    release(feed, "a tampered build",
        "0000000000000000000000000000000000000000000000000000000000000000  " + kImage + "\n");
    FakeInstall install;
    QTemporaryDir dir;
    MainWindow w(installOptions(feed, dir.filePath(QStringLiteral("cutenes.ini")), install));
    bool installed = false;
    QObject::connect(&w, &MainWindow::updateInstalled, [&] { installed = true; });
    QDialog* prompt = promptWithChecksOn(w);
    ASSERT_TRUE(prompt);
    button(prompt, "updateInstall")->click();

    QMessageBox* failed = nullptr;
    ASSERT_TRUE(waitUntil([&] {
        failed = w.findChild<QMessageBox*>(QStringLiteral("updateFailed"));
        return failed && failed->isVisible();
    }, 10000));
    EXPECT_TRUE(failed->text().contains(QStringLiteral("SHA256SUMS"))) << failed->text().toStdString();
    EXPECT_FALSE(installed);
    EXPECT_EQ(install.contents(), "the installed version");
    EXPECT_EQ(install.entries(), QStringList{QStringLiteral("cuteNES.AppImage")});
}

TEST(GuiUpdates, AReleaseWithoutTheBuildsChecksumReplacesNothing)
{
    FakeFeed feed;
    release(feed, "the new version", "abc  something-else\n");
    FakeInstall install;
    QTemporaryDir dir;
    MainWindow w(installOptions(feed, dir.filePath(QStringLiteral("cutenes.ini")), install));
    QDialog* prompt = promptWithChecksOn(w);
    ASSERT_TRUE(prompt);
    button(prompt, "updateInstall")->click();
    EXPECT_TRUE(waitUntil([&] {
        auto* failed = w.findChild<QMessageBox*>(QStringLiteral("updateFailed"));
        return failed && failed->isVisible();
    }, 10000));
    EXPECT_EQ(install.contents(), "the installed version");
    EXPECT_EQ(install.entries(), QStringList{QStringLiteral("cuteNES.AppImage")});
}

TEST(GuiUpdates, CancellingTheDownloadReplacesNothing)
{
    FakeFeed feed;
    release(feed, std::string(1 << 20, 'x'));
    feed.fileDelay(std::chrono::milliseconds(1500)); // long enough to cancel
    FakeInstall install;
    QTemporaryDir dir;
    MainWindow w(installOptions(feed, dir.filePath(QStringLiteral("cutenes.ini")), install));
    bool installed = false;
    QObject::connect(&w, &MainWindow::updateInstalled, [&] { installed = true; });
    QDialog* prompt = promptWithChecksOn(w);
    ASSERT_TRUE(prompt);
    button(prompt, "updateInstall")->click();
    QProgressDialog* progress = nullptr;
    ASSERT_TRUE(waitUntil([&] {
        progress = w.findChild<QProgressDialog*>(QStringLiteral("updateProgress"));
        return progress && progress->isVisible();
    }));
    // As the user does: the button (QProgressDialog::cancel() alone would
    // not say so).
    auto* cancel = progress->findChild<QPushButton*>();
    ASSERT_TRUE(cancel);
    cancel->click();
    settle(4000); // past when the download would have finished

    EXPECT_FALSE(installed);
    EXPECT_EQ(install.contents(), "the installed version");
    EXPECT_EQ(install.entries(), QStringList{QStringLiteral("cuteNES.AppImage")});
}

#if !defined(_WIN32)
TEST(GuiUpdates, NoWriteAccessMeansNoInstall)
{
    FakeFeed feed;
    release(feed, "the new version");
    FakeInstall install;
    QFile::setPermissions(install.dir.path(), QFile::ReadOwner | QFile::ExeOwner);
    QTemporaryDir dir;
    MainWindow w(installOptions(feed, dir.filePath(QStringLiteral("cutenes.ini")), install));
    QDialog* prompt = promptWithChecksOn(w);
    ASSERT_TRUE(prompt);
    EXPECT_EQ(button(prompt, "updateInstall"), nullptr);
    auto* why = prompt->findChild<QLabel*>(QStringLiteral("updateWhyNoInstall"));
    ASSERT_TRUE(why);
    EXPECT_TRUE(why->text().contains(QDir::toNativeSeparators(install.dir.path()))) << why->text().toStdString();
    QFile::setPermissions(install.dir.path(), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
}
#endif
