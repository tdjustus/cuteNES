// G091: gui-coexistence (quickstart A10; spec US6-6, FR-075, SC-008; 002
// US5, SC-006) and the GUI-mode --attach test (002 FR-021(b)).
//
// Coexistence. While the game runs, the "human" surface (the window: its
// gui.* commands and a key on the game view, on the GUI thread) and an "AI"
// surface (a client of the window's HTTP API, on its own thread) change the
// state at the same time: memory writes, saves and loads, resets and input.
// Every change from either side goes through the one session queue and runs
// between frames (002 FR-007), so neither side may ever see half of one:
// - the AI writes a 256-byte block at $0300 whole, a new value each time;
//   every read of the block, by either side, and every state either side
//   loads, holds one value throughout (a torn write or a state captured
//   part way through one would mix two);
// - the slots are shared (US6-7): a state the AI saves into a slot is the
//   one the window loads from it;
// - afterwards both sides see the same frame, CPU, memory and controller.
// The test ROM's program only loops (JMP $C000), so nothing but the test
// writes the block.
//
// --attach. `cutenes-server --attach`, the real executable, reaches the real
// window through the MCP token file its setting writes, and what each side
// does the other sees.

#include "gui_support.hpp"
#include "mainwindow.hpp"

#include <cutenes/qt/emulation_view.hpp>

#include <QFile>
#include <QKeyEvent>
#include <QProcess>
#include <QTemporaryDir>

#include <httplib.h>

#include <gtest/gtest.h>

#include <QApplication>
#include <QElapsedTimer>

#include <atomic>
#include <set>
#include <cstring>
#include <thread>

using cutenes::api::Json;
using cutenes::api::Result;
using cutenes::app::MainWindow;
using cutenes::test::waitUntil;
using cutenes::test::windowOptions;

namespace {

constexpr int kBlock = 0x0300;
constexpr int kBlockSize = 256;

Json call(MainWindow& w, const char* name, const Json& args = Json::object())
{
    const Result r = w.session().call(name, args);
    EXPECT_FALSE(r.isError()) << name << ": " << (r.isError() ? r.error->message : "");
    return r.value;
}

// NROM-128 whose program is JMP $C000 at $C000 (the reset vector), with
// blank CHR: it never writes RAM.
QString loopRom(QTemporaryDir& dir)
{
    QByteArray rom(16 + 16384 + 8192, '\0');
    const char header[] = {'N', 'E', 'S', 0x1A, 1, 1};
    std::memcpy(rom.data(), header, sizeof header);
    rom[16] = static_cast<char>(0x4C);
    rom[17] = 0x00;
    rom[18] = static_cast<char>(0xC0);
    rom[16 + 0x3FFC] = 0x00;
    rom[16 + 0x3FFD] = static_cast<char>(0xC0);
    const QString path = dir.filePath(QStringLiteral("loop.nes"));
    QFile file(path);
    EXPECT_TRUE(file.open(QIODevice::WriteOnly));
    file.write(rom);
    return path;
}

QByteArray bytesOf(const Json& value)
{
    return QByteArray::fromBase64(QByteArray::fromStdString(value["bytes"].get<std::string>()));
}

// One value throughout, or -1.
int uniform(const QByteArray& block)
{
    if (block.size() != kBlockSize) return -1;
    for (char c : block) {
        if (c != block[0]) return -1;
    }
    return static_cast<unsigned char>(block[0]);
}

Json tokenFile(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return Json();
    return Json::parse(f.readAll().toStdString(), nullptr, false);
}

// The AI's side: JSON-RPC calls to the window's API, from its own thread.
class ApiClient {
public:
    explicit ApiClient(const Json& token) : m_token(token.value("token", ""))
    {
        const std::string url = token.value("url", "");
        const std::string rest = url.substr(std::string("http://").size());
        m_client = std::make_unique<httplib::Client>("http://" + rest.substr(0, rest.find('/')));
        m_client->set_bearer_token_auth(m_token);
        m_client->set_read_timeout(30, 0);
    }

    // The command's result, or nullopt (with the reason) on an error.
    std::optional<Json> call(const std::string& method, const Json& params = Json::object(), std::string* error = nullptr)
    {
        const Json request = {{"jsonrpc", "2.0"}, {"id", ++m_id}, {"method", method}, {"params", params}};
        const auto res = m_client->Post("/api/v1/rpc", request.dump(), "application/json");
        if (!res || res->status != 200) {
            if (error) *error = res ? "HTTP " + std::to_string(res->status) : "no response";
            return std::nullopt;
        }
        const Json reply = Json::parse(res->body, nullptr, false);
        if (!reply.contains("result")) {
            if (error) *error = reply.dump();
            return std::nullopt;
        }
        return reply["result"];
    }

private:
    std::string m_token;
    std::unique_ptr<httplib::Client> m_client;
    int m_id = 0;
};

void key(QObject* target, QEvent::Type type, Qt::Key k)
{
    QKeyEvent event(type, k, Qt::NoModifier);
    QApplication::sendEvent(target, &event);
}

// A window with the API (and MCP) on, at free ports, playing the loop ROM.
void serve(MainWindow& w, const QString& rom, bool mcp)
{
    w.show();
    w.activateWindow();
    call(w, "gui.set_setting", {{"key", "emulation.pauseWhenInactive"}, {"value", false}});
    call(w, "gui.set_setting", {{"key", "servers.apiPort"}, {"value", 0}});
    call(w, "gui.set_setting", {{"key", "servers.mcpPort"}, {"value", 0}});
    call(w, "gui.set_setting", {{"key", mcp ? "servers.mcpEnabled" : "servers.apiEnabled"}, {"value", true}});
    call(w, "gui.open_rom", {{"path", rom.toStdString()}});
}

} // namespace

TEST(GuiCoexistence, InterleavedChangesFromBothSurfacesNeverShowATornState)
{
    QTemporaryDir dir;
    const MainWindow::Options options = windowOptions();
    MainWindow w(options);
    serve(w, loopRom(dir), false);
    const QString apiFile = options.tokenDir + QStringLiteral("/api.token");
    ASSERT_TRUE(waitUntil([&] { return tokenFile(apiFile).contains("url"); }));
    ASSERT_EQ(call(w, "core.run_state")["runState"], "Running");

    // A first block, saved into slot 2 by the window, so the AI's loads of
    // it below always have a state to load.
    call(w, "core.write_memory", {{"addr", kBlock}, {"bytes", QByteArray(kBlockSize, '\x01').toBase64().toStdString()}});
    call(w, "gui.save_state", {{"slot", 2}});

    // The AI: write the block whole with a new value, read it back, and
    // now and then save into slot 3, load slot 2, reset or press buttons.
    constexpr int kRounds = 300;
    std::atomic<bool> done{false};
    std::atomic<int> aiTorn{0};
    std::atomic<int> aiErrors{0};
    std::string firstError;
    std::thread ai([&] {
        ApiClient api(tokenFile(apiFile));
        std::string error;
        const auto check = [&](const std::optional<Json>& r) {
            if (!r && aiErrors++ == 0) firstError = error;
            return r;
        };
        for (int i = 2; i < kRounds + 2; ++i) {
            const QByteArray block(kBlockSize, static_cast<char>(i & 0xFF));
            check(api.call("core.write_memory", {{"addr", kBlock}, {"bytes", block.toBase64().toStdString()}}, &error));
            if (const auto r = check(api.call("core.read_memory", {{"addr", kBlock}, {"length", kBlockSize}}, &error))) {
                if (uniform(bytesOf(*r)) < 0) ++aiTorn;
            }
            if (i % 7 == 0) check(api.call("gui.save_state", {{"slot", 3}}, &error));
            if (i % 11 == 0) {
                check(api.call("gui.load_state", {{"slot", 2}}, &error));
                if (const auto r = check(api.call("core.read_memory", {{"addr", kBlock}, {"length", kBlockSize}}, &error))) {
                    if (uniform(bytesOf(*r)) < 0) ++aiTorn;
                }
            }
            if (i % 13 == 0) check(api.call("gui.reset", Json::object(), &error));
            if (i % 5 == 0) check(api.call("core.set_controller", {{"port", 2}, {"buttons", Json::array({"B"})}}, &error));
        }
        done = true;
    });

    // The human, meanwhile: quick save and load, load the AI's slot, reset,
    // and press A on the game view; every block read or loaded is whole.
    int rounds = 0;
    int humanTorn = 0;
    std::set<int> seen;
    const auto readBlock = [&] {
        const int value = uniform(bytesOf(call(w, "core.read_memory", {{"addr", kBlock}, {"length", kBlockSize}})));
        if (value < 0) ++humanTorn;
        seen.insert(value);
    };
    while (!done) {
        call(w, "gui.quick_save");
        readBlock();
        call(w, "gui.quick_load");
        readBlock();
        if (rounds % 3 == 0 && !call(w, "gui.list_slots")["slots"][2].value("empty", true)) {
            call(w, "gui.load_state", {{"slot", 3}});
            readBlock();
        }
        if (rounds % 4 == 0) call(w, "gui.reset");
        key(w.view(), rounds % 2 ? QEvent::KeyRelease : QEvent::KeyPress, Qt::Key_X);
        QCoreApplication::processEvents();
        ++rounds;
    }
    ai.join();
    key(w.view(), QEvent::KeyRelease, Qt::Key_X);
    EXPECT_EQ(aiErrors, 0) << firstError;
    EXPECT_EQ(aiTorn, 0);
    EXPECT_EQ(humanTorn, 0);
    EXPECT_GT(rounds, 10) << "the two surfaces did not overlap";
    EXPECT_GT(seen.size(), 3u) << "the window saw the AI's writes as they came";

    // The slots are shared: what the AI saved in slot 3 is what the window
    // loads from it.
    ApiClient api(tokenFile(apiFile));
    call(w, "core.pause");
    ASSERT_TRUE(api.call("core.write_memory", {{"addr", kBlock}, {"bytes", QByteArray(kBlockSize, '\x5A').toBase64().toStdString()}}));
    ASSERT_TRUE(api.call("gui.save_state", {{"slot", 3}}));
    call(w, "core.write_memory", {{"addr", kBlock}, {"bytes", QByteArray(kBlockSize, '\x00').toBase64().toStdString()}});
    call(w, "gui.load_state", {{"slot", 3}});
    EXPECT_EQ(uniform(bytesOf(call(w, "core.read_memory", {{"addr", kBlock}, {"length", kBlockSize}}))), 0x5A);

    // Afterwards, paused, both see one machine.
    call(w, "core.pause");
    const auto aiHash = api.call("core.frame_hash");
    const auto aiCpu = api.call("core.get_cpu");
    const auto aiBlock = api.call("core.read_memory", {{"addr", kBlock}, {"length", kBlockSize}});
    const auto aiPad = api.call("core.get_controller", {{"port", 2}});
    ASSERT_TRUE(aiHash && aiCpu && aiBlock && aiPad);
    EXPECT_EQ((*aiHash)["frameHash"], call(w, "core.frame_hash")["frameHash"]);
    EXPECT_EQ((*aiHash)["frame"], call(w, "core.frame_hash")["frame"]);
    EXPECT_EQ((*aiCpu)["cpu"], call(w, "core.get_cpu")["cpu"]);
    EXPECT_EQ((*aiBlock)["bytes"], call(w, "core.read_memory", {{"addr", kBlock}, {"length", kBlockSize}})["bytes"]);
    EXPECT_EQ((*aiPad)["buttons"], call(w, "core.get_controller", {{"port", 2}})["buttons"]);
}

namespace {

// cutenes-server --attach on stdio, as an MCP client starts it.
class Bridge {
public:
    explicit Bridge(const QString& tokenFile)
    {
        m_process.start(QStringLiteral(CUTENES_SERVER_EXE),
            {QStringLiteral("--attach"), QStringLiteral("--mcp-token-file"), tokenFile});
        started = m_process.waitForStarted(10000);
    }
    ~Bridge()
    {
        if (m_process.state() != QProcess::NotRunning) {
            m_process.kill();
            m_process.waitForFinished(5000);
        }
    }

    // A 2026-07-28 request (its version in _meta) and its response.
    Json request(const std::string& method, Json params = Json::object())
    {
        params["_meta"] = {{"io.modelcontextprotocol/protocolVersion", "2026-07-28"},
            {"io.modelcontextprotocol/clientCapabilities", Json::object()},
            {"io.modelcontextprotocol/clientInfo", {{"name", "cutenes-gui-tests"}, {"version", "1"}}}};
        const int id = ++m_id;
        const Json message = {{"jsonrpc", "2.0"}, {"id", id}, {"method", method}, {"params", params}};
        m_process.write(QByteArray::fromStdString(message.dump() + "\n"));
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 60000) {
            while (m_process.canReadLine()) {
                const Json reply = Json::parse(m_process.readLine().toStdString(), nullptr, false);
                if (reply.is_object() && reply.value("id", -1) == id && !reply.contains("method")) return reply;
            }
            // The window's session runs on its own thread, but the window's
            // posted work (its pages, its windows) needs the GUI loop.
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            m_process.waitForReadyRead(10);
        }
        ADD_FAILURE() << "no response to " << method;
        return Json();
    }

    // A tool's structured result.
    Json tool(const std::string& name, const Json& args = Json::object())
    {
        const Json reply = request("tools/call", {{"name", name}, {"arguments", args.is_null() ? Json::object() : args}});
        const Json result = reply.value("result", Json::object());
        EXPECT_FALSE(result.value("isError", false)) << name << ": " << reply.dump().substr(0, 400);
        return result.value("structuredContent", Json::object());
    }

    int finish()
    {
        m_process.closeWriteChannel();
        if (!m_process.waitForFinished(10000)) return -1;
        return m_process.exitCode();
    }

    bool started = false;

private:
    QProcess m_process;
    int m_id = 0;
};

} // namespace

TEST(GuiCoexistence, AttachReachesTheRealWindow)
{
    QTemporaryDir dir;
    const QString rom = loopRom(dir);
    const MainWindow::Options options = windowOptions();
    MainWindow w(options);
    w.show();
    call(w, "gui.set_setting", {{"key", "emulation.pauseWhenInactive"}, {"value", false}});
    call(w, "gui.set_setting", {{"key", "servers.apiPort"}, {"value", 0}});
    call(w, "gui.set_setting", {{"key", "servers.mcpPort"}, {"value", 0}});
    call(w, "gui.set_setting", {{"key", "servers.mcpEnabled"}, {"value", true}});
    const QString mcpFile = options.tokenDir + QStringLiteral("/mcp.token");
    ASSERT_TRUE(waitUntil([&] { return tokenFile(mcpFile).contains("url"); }));

    Bridge bridge(mcpFile);
    ASSERT_TRUE(bridge.started) << CUTENES_SERVER_EXE;

    // The window's catalog, gui.* included.
    const Json list = bridge.request("tools/list");
    std::vector<std::string> tools;
    for (const Json& t : list["result"]["tools"]) tools.push_back(t["name"]);
    EXPECT_EQ(tools, w.session().registry().names());

    // What the bridge's client does, the window does: it opens the game,
    // which the window then shows.
    bridge.tool("gui.open_rom", {{"path", rom.toStdString()}});
    EXPECT_TRUE(waitUntil([&] { return call(w, "gui.state")["page"] == "game"; }));
    EXPECT_EQ(call(w, "gui.state")["game"], true);
    bridge.tool("gui.toggle_pause");
    EXPECT_EQ(call(w, "core.run_state")["runState"], "Paused");
    const Json ran = bridge.tool("core.run_frames", {{"frames", 30}});
    EXPECT_EQ(ran["framesRun"], 30);
    EXPECT_EQ(call(w, "core.frame_hash")["frame"], ran["frame"]);

    // And what the window does, the client sees.
    call(w, "core.write_memory", {{"addr", kBlock}, {"bytes", QByteArray(4, '\x42').toBase64().toStdString()}});
    EXPECT_EQ(bridge.tool("core.read_memory", {{"addr", kBlock}, {"length", 4}})["bytes"], QByteArray(4, '\x42').toBase64().toStdString());
    call(w, "gui.close_game");
    EXPECT_EQ(bridge.tool("gui.state")["game"], false);

    EXPECT_EQ(bridge.finish(), 0);
}
