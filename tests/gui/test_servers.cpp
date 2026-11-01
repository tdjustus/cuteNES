// G035 (A9): Settings > Servers (spec US6, FR-071-FR-073; 002 FR-009,
// FR-020). Off by default with no port open; MCP on starts the API and
// writes the token file that `cutenes-server --attach` reads; the token
// opens the window's own session; API off ends clients' calls with
// ShuttingDown while the game plays on; an indicator shows while a client
// is there and says when automation paused the game.

#include "gui_support.hpp"
#include "mainwindow.hpp"

#include <cutenes/api/token.hpp>
#include <cutenes/mcp/bridge.hpp>

#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QPushButton>

#include <httplib.h>

#include <gtest/gtest.h>

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

// Options whose token files go in the test's own folder, with ports any
// free one (0) so tests never collide.
MainWindow::Options serverOptions(QString* tokenDir)
{
    QString folder;
    MainWindow::Options options = windowOptions(&folder);
    options.tokenDir = folder + QStringLiteral("/tokens");
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
        MainWindow w(options); // turned on, they start with the next launch
        EXPECT_TRUE(waitUntil([&] { return QFile::exists(tokens + QStringLiteral("/mcp.token")); }));
    }
}
