// G035 (A9): Settings > Servers (spec US6, FR-071-FR-073; 002 FR-009,
// FR-020). Off by default with no port open; MCP on starts the API and
// writes the token file that `cutenes-server --attach` reads; the token
// opens the window's own session; API off ends clients' calls with
// ShuttingDown while the game plays on; an indicator shows while a client
// is there and says when automation paused the game, and opens Settings >
// Servers, whose Disable Automation cuts every client off (#233). The servers last one run unless the user
// confirms they should start with cuteNES (#233).

#include "gui_support.hpp"
#include "mainwindow.hpp"

#include <cutenes/api/token.hpp>
#include <cutenes/mcp/bridge.hpp>

#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>

#include <httplib.h>

#include <gtest/gtest.h>

#include <thread>

using cutenes::api::Json;
using cutenes::api::Result;
using cutenes::app::MainWindow;
using cutenes::test::waitUntil;
using cutenes::test::windowOptions;

namespace {

const QString kRom = QStringLiteral(CUTENES_TEST_ROMS "/instr_test-v5/rom_singles/01-basics.nes");

Json call(MainWindow& w, const char* name, const Json& args = Json::object())
{
    const Result r = w.session().call(name, args);
    EXPECT_FALSE(r.isError()) << name << ": " << (r.isError() ? r.error->message : "");
    return r.value;
}

// Options whose token folder the test reads (windowOptions gives every
// window its own); ports are any free one (0) so tests never collide.
MainWindow::Options serverOptions(QString* tokenDir)
{
    MainWindow::Options options = windowOptions();
    if (tokenDir) *tokenDir = options.tokenDir;
    return options;
}

void anyPorts(MainWindow& w)
{
    call(w, "gui.set_setting", {{"key", "servers.apiPort"}, {"value", 0}});
    call(w, "gui.set_setting", {{"key", "servers.mcpPort"}, {"value", 0}});
}

struct TokenFile {
    std::string url;
    std::string token;
};

std::optional<TokenFile> readToken(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return std::nullopt;
    const Json j = Json::parse(f.readAll().toStdString(), nullptr, false);
    if (!j.is_object()) return std::nullopt;
    return TokenFile{j.value("url", ""), j.value("token", "")};
}

// A JSON-RPC call to the API at `url` (http://127.0.0.1:N/api/v1).
std::optional<Json> rpc(const TokenFile& api, const std::string& method, const Json& params = Json::object())
{
    const std::string rest = api.url.substr(std::string("http://").size());
    const std::string hostPort = rest.substr(0, rest.find('/'));
    httplib::Client client("http://" + hostPort);
    client.set_bearer_token_auth(api.token);
    client.set_read_timeout(10, 0);
    const Json request = {{"jsonrpc", "2.0"}, {"id", 1}, {"method", method}, {"params", params}};
    const auto res = client.Post("/api/v1/rpc", request.dump(), "application/json");
    if (!res || res->status != 200) return std::nullopt;
    return Json::parse(res->body, nullptr, false);
}

} // namespace

TEST(GuiServers, OffByDefaultWithNoPortOpen)
{
    QString tokens;
    MainWindow w(serverOptions(&tokens));
    const Json values = call(w, "gui.get_settings", {{"group", "servers"}})["values"]["servers"];
    EXPECT_EQ(values["apiEnabled"], false);
    EXPECT_EQ(values["mcpEnabled"], false);
    EXPECT_EQ(values["apiPort"], 8765) << "data-model ServerSettings";
    EXPECT_EQ(values["mcpPort"], 8766);
    EXPECT_FALSE(QFile::exists(tokens + QStringLiteral("/api.token")));
    EXPECT_FALSE(QFile::exists(tokens + QStringLiteral("/mcp.token")));
    EXPECT_FALSE(w.findChild<QPushButton*>(QStringLiteral("automationIndicator"))->isVisible());
}

TEST(GuiServers, McpOnStartsTheApiAndWritesTheTokenFiles)
{
    QString tokens;
    MainWindow w(serverOptions(&tokens));
    w.show();
    anyPorts(w);
    call(w, "gui.set_setting", {{"key", "servers.mcpEnabled"}, {"value", true}});
    const QString mcpFile = tokens + QStringLiteral("/mcp.token");
    const QString apiFile = tokens + QStringLiteral("/api.token");
    ASSERT_TRUE(waitUntil([&] { return QFile::exists(mcpFile) && QFile::exists(apiFile); }));
    EXPECT_EQ(call(w, "gui.get_settings", {{"group", "servers"}})["values"]["servers"]["apiEnabled"], true)
        << "MCP needs the API";
    std::string reason;
    EXPECT_TRUE(cutenes::api::token::isOwnerOnly(mcpFile.toStdString(), &reason)) << reason;

    // What --attach does: read the file and reach the endpoint with it.
    std::string error;
    const auto endpoint = cutenes::mcp::readTokenFile(mcpFile.toStdString(), error);
    ASSERT_TRUE(endpoint) << error;
    EXPECT_EQ(cutenes::mcp::probe(*endpoint, error), 0) << error;

    // The API token opens this window's own session, gui.* included.
    const auto api = readToken(apiFile);
    ASSERT_TRUE(api);
    const auto catalog = rpc(*api, "api.catalog");
    ASSERT_TRUE(catalog);
    bool hasGui = false;
    for (const Json& c : (*catalog)["result"]["commands"]) hasGui = hasGui || c.value("name", "") == "gui.take_screenshot";
    EXPECT_TRUE(hasGui) << catalog->dump().substr(0, 300);
}

TEST(GuiServers, ApiOffEndsTheListenersAndThePlayGoesOn)
{
    QString tokens;
    MainWindow w(serverOptions(&tokens));
    w.show();
    call(w, "gui.open_rom", {{"path", kRom.toStdString()}});
    anyPorts(w);
    call(w, "gui.set_setting", {{"key", "servers.mcpEnabled"}, {"value", true}});
    const QString apiFile = tokens + QStringLiteral("/api.token");
    ASSERT_TRUE(waitUntil([&] { return QFile::exists(apiFile); }));
    const auto api = readToken(apiFile);
    ASSERT_TRUE(api);

    // A client turns the API off through the API itself: it gets its reply.
    const auto off = rpc(*api, "gui.set_setting", {{"key", "servers.apiEnabled"}, {"value", false}});
    ASSERT_TRUE(off);
    EXPECT_TRUE(off->contains("result")) << off->dump();
    EXPECT_TRUE(waitUntil([&] {
        return !QFile::exists(apiFile) && !QFile::exists(tokens + QStringLiteral("/mcp.token"));
    })) << "both listeners stop and their token files go";
    EXPECT_FALSE(rpc(*api, "api.version")) << "nothing listens there now";
    EXPECT_EQ(call(w, "gui.get_settings", {{"group", "servers"}})["values"]["servers"]["mcpEnabled"], false);
    const auto frame = call(w, "core.counters")["frame"].get<std::uint64_t>();
    EXPECT_TRUE(waitUntil([&] { return call(w, "core.counters")["frame"].get<std::uint64_t>() > frame + 10; }))
        << "the game plays on";
}

// The token files are shared by every cuteNES on the machine, and the
// latest one to start its server owns them: a window removes a token file
// only when the file still holds its own token. A window that never served
// (servers off, or a second copy of the application) leaves alone those of
// the instance a client is attached to, and so does one whose files
// another launch has replaced since.
TEST(GuiServers, AWindowRemovesOnlyItsOwnTokenFiles)
{
    QString tokens;
    const MainWindow::Options options = serverOptions(&tokens);
    const QString mcpFile = tokens + QStringLiteral("/mcp.token");
    const QString apiFile = tokens + QStringLiteral("/api.token");
    const auto otherLaunch = [&] {
        std::string error;
        EXPECT_TRUE(cutenes::api::token::writeFile(mcpFile.toStdString(), "http://127.0.0.1:1/mcp", "other", error));
        EXPECT_TRUE(cutenes::api::token::writeFile(apiFile.toStdString(), "http://127.0.0.1:2/api/v1", "other", error));
    };
    const auto stillTheOtherLaunchs = [&] {
        const auto mcp = readToken(mcpFile);
        const auto api = readToken(apiFile);
        return mcp && api && mcp->token == "other" && api->token == "other";
    };

    otherLaunch();
    {
        MainWindow w(options);
        w.show();
    }
    EXPECT_TRUE(stillTheOtherLaunchs()) << "a window that never served removed them";

    otherLaunch();
    {
        MainWindow w(options);
        w.show();
        anyPorts(w);
        call(w, "gui.set_setting", {{"key", "servers.mcpEnabled"}, {"value", true}});
        std::optional<TokenFile> own;
        ASSERT_TRUE(waitUntil([&] {
            own = readToken(apiFile);
            return own && own->token != "other" && QFile::exists(mcpFile);
        })) << "the window took the token files over";
        otherLaunch();
        call(w, "gui.set_setting", {{"key", "servers.mcpEnabled"}, {"value", false}});
        call(w, "gui.set_setting", {{"key", "servers.apiEnabled"}, {"value", false}});
        ASSERT_TRUE(waitUntil([&] { return !rpc(*own, "api.version"); })) << "the window's API stopped";
        EXPECT_TRUE(stillTheOtherLaunchs()) << "turning the servers off removed another launch's files";
    }
    EXPECT_TRUE(stillTheOtherLaunchs()) << "closing the window removed another launch's files";
}

TEST(GuiServers, TheIndicatorShowsAClientAndAnAutomationPause)
{
    QString tokens;
    MainWindow w(serverOptions(&tokens));
    w.show();
    call(w, "gui.open_rom", {{"path", kRom.toStdString()}});
    anyPorts(w);
    call(w, "gui.set_setting", {{"key", "servers.apiEnabled"}, {"value", true}});
    const QString apiFile = tokens + QStringLiteral("/api.token");
    ASSERT_TRUE(waitUntil([&] { return QFile::exists(apiFile); }));
    auto* indicator = w.findChild<QPushButton*>(QStringLiteral("automationIndicator"));
    ASSERT_NE(indicator, nullptr);
    EXPECT_FALSE(indicator->isVisible()) << "on, but no client yet";

    const auto api = readToken(apiFile);
    ASSERT_TRUE(api);
    // An advance command pauses autonomous play for automation (002 R5).
    ASSERT_TRUE(rpc(*api, "core.run_frames", {{"frames", 1}}));
    EXPECT_TRUE(waitUntil([&] { return indicator->isVisible(); }));
    EXPECT_TRUE(waitUntil([&] { return indicator->text().contains(QStringLiteral("PAUSED")); }))
        << indicator->text().toStdString();
    EXPECT_TRUE(indicator->toolTip().contains(QStringLiteral("Paused by automation")));

    // The user takes control back.
    call(w, "gui.toggle_pause");
    EXPECT_EQ(call(w, "core.run_state")["runState"], "Running");
    EXPECT_TRUE(waitUntil([&] { return !indicator->text().contains(QStringLiteral("PAUSED")); }));
}

TEST(GuiServers, TheSettingsPageShowsAndChangesTheServers)
{
    QString tokens;
    MainWindow::Options options = serverOptions(&tokens);
    {
        MainWindow w(options);
        w.show();
        anyPorts(w);
        call(w, "gui.show_settings", {{"page", "servers"}});
        QCheckBox* mcp = nullptr;
        ASSERT_TRUE(waitUntil([&] { return (mcp = w.findChild<QCheckBox*>(QStringLiteral("mcpEnabled"))) != nullptr; }));
        auto* api = w.findChild<QCheckBox*>(QStringLiteral("apiEnabled"));
        EXPECT_FALSE(api->isChecked());
        mcp->setChecked(true); // the user turns MCP on
        EXPECT_TRUE(waitUntil([&] { return QFile::exists(tokens + QStringLiteral("/mcp.token")); }));
        EXPECT_TRUE(waitUntil([&] { return api->isChecked(); })) << "the API follows";
        auto* copy = w.findChild<QPushButton*>(QStringLiteral("copyMcpToken"));
        ASSERT_NE(copy, nullptr);
        EXPECT_TRUE(copy->isEnabled());
    }
    EXPECT_FALSE(QFile::exists(tokens + QStringLiteral("/mcp.token"))) << "closing the window stops the servers";
    {
        MainWindow w(options); // #233: on for one run unless they start with cuteNES
        w.show();
        QCoreApplication::processEvents();
        EXPECT_FALSE(QFile::exists(tokens + QStringLiteral("/mcp.token")));
        const Json saved = call(w, "gui.get_settings", {{"group", "servers"}})["values"]["servers"];
        EXPECT_EQ(saved["apiEnabled"], false);
        EXPECT_EQ(saved["mcpEnabled"], false);
    }
}

// The Disable Automation button at the top of Settings > Servers (#233),
// once the page is shown.
QPushButton* disableButton(MainWindow& w)
{
    QPushButton* button = nullptr;
    waitUntil([&] {
        button = w.findChild<QPushButton*>(QStringLiteral("disableAutomation"));
        return button && button->isVisible();
    });
    return button;
}

// #233: clicking the automation indicator opens Settings > Servers, whose
// Disable Automation turns every server setting off at once. It cuts a
// call in progress (ShuttingDown) and the token files go, so nothing can
// carry on or reconnect. The buttons the client held are let go, and the
// game plays on.
TEST(GuiServers, DisableAutomationCutsTheClientOffAndTurnsEverythingOff)
{
    QString tokens;
    const MainWindow::Options options = serverOptions(&tokens);
    {
        // Kept across launches, as Settings > Servers allows after asking.
        QSettings ini(options.settingsFile, QSettings::IniFormat);
        ini.setValue(QStringLiteral("servers/startWithApp"), true);
    }
    MainWindow w(options);
    w.show();
    call(w, "gui.open_rom", {{"path", kRom.toStdString()}});
    anyPorts(w);
    call(w, "gui.set_setting", {{"key", "servers.mcpEnabled"}, {"value", true}});
    const QString apiFile = tokens + QStringLiteral("/api.token");
    const QString mcpFile = tokens + QStringLiteral("/mcp.token");
    ASSERT_TRUE(waitUntil([&] { return QFile::exists(apiFile) && QFile::exists(mcpFile); }));
    const auto api = readToken(apiFile);
    ASSERT_TRUE(api);
    EXPECT_EQ(w.findChild<QPushButton*>(QStringLiteral("automationStop")), nullptr) << "no Stop on the indicator";

    ASSERT_TRUE(rpc(*api, "core.set_controller", {{"port", 1}, {"buttons", Json::array({"A", "Right"})}}));
    EXPECT_EQ(call(w, "core.get_controller", {{"port", 1}})["buttons"].size(), 2u);
    auto* indicator = w.findChild<QPushButton*>(QStringLiteral("automationIndicator"));
    ASSERT_TRUE(waitUntil([&] { return indicator->isVisible(); }));
    indicator->click();
    QPushButton* disable = disableButton(w);
    ASSERT_NE(disable, nullptr) << "the indicator opens Settings > Servers";
    EXPECT_TRUE(disable->isEnabled());

    // The longest call a client may make (maxRunFrames): seconds of play,
    // far longer than the moment it takes to reach the listener.
    std::optional<Json> longCall;
    std::thread client([&] { longCall = rpc(*api, "core.run_frames", {{"frames", 3600}}); });
    waitUntil([] { return false; }, 300);
    disable->click();
    client.join(); // within the rpc helper's 10 s read timeout
    ASSERT_TRUE(longCall) << "the call got no reply";
    ASSERT_TRUE(longCall->contains("error")) << longCall->dump();
    EXPECT_EQ((*longCall)["error"]["data"].value("name", ""), "ShuttingDown") << longCall->dump();

    EXPECT_FALSE(QFile::exists(apiFile)) << "the token files go";
    EXPECT_FALSE(QFile::exists(mcpFile));
    EXPECT_FALSE(rpc(*api, "api.version")) << "nothing listens there now";
    const Json saved = call(w, "gui.get_settings", {{"group", "servers"}})["values"]["servers"];
    EXPECT_EQ(saved["apiEnabled"], false);
    EXPECT_EQ(saved["mcpEnabled"], false);
    EXPECT_EQ(saved["startWithApp"], false) << "every server setting goes off";
    EXPECT_TRUE(call(w, "core.get_controller", {{"port", 1}})["buttons"].empty()) << "the client's buttons are let go";
    EXPECT_TRUE(waitUntil([&] { return !disable->isEnabled() && !indicator->isVisible(); }))
        << "nothing is left on to disable";
    EXPECT_FALSE(w.findChild<QCheckBox*>(QStringLiteral("serversStartWithApp"))->isChecked());
}

// #233: Disable Automation is available only while something is on, and it
// also ends servers that --api or --mcp started for one run.
TEST(GuiServers, DisableAutomationEndsRunOnlyServersToo)
{
    {
        MainWindow w(serverOptions(nullptr));
        w.show();
        call(w, "gui.show_settings", {{"page", "servers"}});
        QPushButton* disable = disableButton(w);
        ASSERT_NE(disable, nullptr);
        EXPECT_FALSE(disable->isEnabled()) << "everything is off already";
    }
    QString tokens;
    MainWindow::Options options = serverOptions(&tokens);
    options.runApi = true;
    options.runApiPort = 0;
    MainWindow w(options);
    w.show();
    const QString apiFile = tokens + QStringLiteral("/api.token");
    ASSERT_TRUE(waitUntil([&] { return QFile::exists(apiFile); }));
    const auto api = readToken(apiFile);
    ASSERT_TRUE(api);
    call(w, "gui.show_settings", {{"page", "servers"}});
    QPushButton* disable = disableButton(w);
    ASSERT_NE(disable, nullptr);
    EXPECT_TRUE(waitUntil([&] { return disable->isEnabled(); })) << "a run-only server is on";
    disable->click();
    EXPECT_TRUE(waitUntil([&] { return !QFile::exists(apiFile); }));
    EXPECT_FALSE(rpc(*api, "api.version"));
}

// #233: the servers are saved as off when the window closes, and a launch
// that finds them on (a crash, or a version from before #233) starts none.
TEST(GuiServers, ServersAreOffAtTheNextLaunch)
{
    QString tokens;
    MainWindow::Options options = serverOptions(&tokens);
    const QString mcpFile = tokens + QStringLiteral("/mcp.token");
    {
        MainWindow w(options);
        w.show();
        anyPorts(w);
        call(w, "gui.set_setting", {{"key", "servers.mcpEnabled"}, {"value", true}});
        ASSERT_TRUE(waitUntil([&] { return QFile::exists(mcpFile); }));
    }
    {
        QSettings ini(options.settingsFile, QSettings::IniFormat);
        EXPECT_FALSE(ini.value(QStringLiteral("servers/mcpEnabled")).toBool()) << "saved as off on close";
        EXPECT_FALSE(ini.value(QStringLiteral("servers/apiEnabled")).toBool());
        // As a crash, or an earlier version, would leave them.
        ini.setValue(QStringLiteral("servers/apiEnabled"), true);
        ini.setValue(QStringLiteral("servers/mcpEnabled"), true);
    }
    MainWindow w(options);
    w.show();
    QCoreApplication::processEvents();
    EXPECT_FALSE(QFile::exists(mcpFile)) << "on in the file is not consent to start";
    EXPECT_FALSE(QFile::exists(tokens + QStringLiteral("/api.token")));
    const Json saved = call(w, "gui.get_settings", {{"group", "servers"}})["values"]["servers"];
    EXPECT_EQ(saved["mcpEnabled"], false);
    EXPECT_EQ(saved["startWithApp"], false) << "off by default";
}

// #233: starting the servers with cuteNES is turned on only in Settings >
// Servers, after a confirmation that says what it allows. Cancel leaves it
// off; Turn On keeps the servers across launches.
TEST(GuiServers, StartingWithCuteNesNeedsTheUsersConfirmation)
{
    QString tokens;
    MainWindow::Options options = serverOptions(&tokens);
    const QString mcpFile = tokens + QStringLiteral("/mcp.token");
    const auto confirmation = [&](MainWindow& w) -> QMessageBox* {
        QMessageBox* box = nullptr;
        waitUntil([&] {
            box = w.findChild<QMessageBox*>(QStringLiteral("serversStartupConfirmation"));
            return box && box->isVisible();
        });
        return box;
    };
    {
        MainWindow w(options);
        w.show();
        anyPorts(w);
        call(w, "gui.show_settings", {{"page", "servers"}});
        QCheckBox* startup = nullptr;
        ASSERT_TRUE(waitUntil([&] {
            return (startup = w.findChild<QCheckBox*>(QStringLiteral("serversStartWithApp"))) != nullptr;
        }));
        EXPECT_FALSE(startup->isChecked()) << "off by default";

        startup->click();
        QMessageBox* box = confirmation(w);
        ASSERT_NE(box, nullptr) << "turning it on asks first";
        const QString said = box->text() + QLatin1Char('\n') + box->informativeText();
        for (const char* phrase : {"Every time cuteNES starts", "token file", "control cuteNES", "this computer",
                 "until you turn it off"}) {
            EXPECT_TRUE(said.contains(QString::fromLatin1(phrase))) << phrase << " in: " << said.toStdString();
        }
        box->button(QMessageBox::Cancel)->click();
        EXPECT_TRUE(waitUntil([&] { return !startup->isChecked(); })) << "Cancel leaves it off";
        EXPECT_EQ(call(w, "gui.get_settings", {{"group", "servers"}})["values"]["servers"]["startWithApp"], false);

        startup->click();
        box = confirmation(w);
        ASSERT_NE(box, nullptr);
        QAbstractButton* turnOn = nullptr;
        for (QAbstractButton* b : box->buttons()) {
            if (b->text() == QStringLiteral("Turn On")) turnOn = b;
        }
        ASSERT_NE(turnOn, nullptr);
        turnOn->click();
        EXPECT_TRUE(waitUntil([&] {
            return call(w, "gui.get_settings", {{"group", "servers"}})["values"]["servers"]["startWithApp"] == true;
        }));
        EXPECT_TRUE(startup->isChecked());
        call(w, "gui.set_setting", {{"key", "servers.mcpEnabled"}, {"value", true}});
        ASSERT_TRUE(waitUntil([&] { return QFile::exists(mcpFile); }));
    }
    {
        MainWindow w(options); // they start with the next launch, without asking
        EXPECT_TRUE(waitUntil([&] { return QFile::exists(mcpFile); }));
        // Turning it off needs no confirmation, and the servers are off at the
        // next launch again.
        call(w, "gui.set_setting", {{"key", "servers.startWithApp"}, {"value", false}});
    }
    MainWindow w(options);
    QCoreApplication::processEvents();
    EXPECT_FALSE(QFile::exists(mcpFile));
}

// #233: a client could skip the confirmation and make itself start with
// every launch, so automation can turn the setting off but never on.
TEST(GuiServers, AutomationCannotMakeTheServersStartWithCuteNes)
{
    MainWindow w(serverOptions(nullptr));
    const Result on = w.session().call("gui.set_setting", {{"key", "servers.startWithApp"}, {"value", true}});
    ASSERT_TRUE(on.isError());
    EXPECT_EQ(on.error->name, "InvalidArguments");
    EXPECT_NE(on.error->message.find("Settings > Servers"), std::string::npos) << on.error->message;
    EXPECT_EQ(call(w, "gui.get_settings", {{"group", "servers"}})["values"]["servers"]["startWithApp"], false);
    const Result off = w.session().call("gui.set_setting", {{"key", "servers.startWithApp"}, {"value", false}});
    EXPECT_FALSE(off.isError()) << (off.isError() ? off.error->message : "");
    const Json schema = call(w, "gui.get_settings", {{"group", "servers"}})["schema"];
    EXPECT_TRUE(schema.contains("servers.startWithApp")) << schema.dump();
}

// contracts/cli.md "Run-only options": --mcp turns both servers on for this
// run and leaves the saved settings alone; a change to the Servers settings
// then decides, as it would without the option.
TEST(GuiServers, RunOnlyServersLeaveTheSettingsAlone)
{
    QString tokens;
    MainWindow::Options options = serverOptions(&tokens);
    options.runApi = true;
    options.runMcp = true;
    options.runApiPort = 0;
    options.runMcpPort = 0;
    MainWindow w(options);
    const QString mcpFile = tokens + QStringLiteral("/mcp.token");
    const QString apiFile = tokens + QStringLiteral("/api.token");
    ASSERT_TRUE(waitUntil([&] { return QFile::exists(mcpFile) && QFile::exists(apiFile); }));
    std::string error;
    const auto endpoint = cutenes::mcp::readTokenFile(mcpFile.toStdString(), error);
    ASSERT_TRUE(endpoint) << error;
    EXPECT_EQ(cutenes::mcp::probe(*endpoint, error), 0) << error;

    const Json saved = call(w, "gui.get_settings", {{"group", "servers"}})["values"]["servers"];
    EXPECT_EQ(saved["apiEnabled"], false) << "run-only";
    EXPECT_EQ(saved["mcpEnabled"], false);
    EXPECT_EQ(saved["apiPort"], 8765) << "the port given is not saved either";
    EXPECT_EQ(saved["mcpPort"], 8766);

    // Any change to the Servers settings: they decide from then on.
    call(w, "gui.set_setting", {{"key", "servers.mcpEnabled"}, {"value", false}});
    EXPECT_TRUE(waitUntil([&] { return !QFile::exists(mcpFile) && !QFile::exists(apiFile); }));
}
