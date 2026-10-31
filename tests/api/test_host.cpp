// Q9 (A050, FR-023): the host extension point. A FakeHost's gui.*
// commands appear in api.catalog and MCP's tools/list, run on the emulation
// thread, and adding or removing them is a catalog_changed event and an MCP
// list-changed notification in both revisions.

#include "fake_host.hpp"
#include "support.hpp"

#include <cutenes/mcp/server.hpp>

#include <atomic>

using namespace cutenes::api;
using namespace cutenes::api::test;
namespace mcp = cutenes::mcp;

namespace {

SessionConfig hostConfig(FakeHost& host)
{
    SessionConfig c = testConfig();
    c.host = &host;
    return c;
}

std::vector<std::string> names(const Json& list, const char* key = "name")
{
    std::vector<std::string> out;
    for (const auto& item : list) out.push_back(item.value(key, ""));
    return out;
}

bool contains(const std::vector<std::string>& v, const std::string& s)
{
    return std::find(v.begin(), v.end(), s) != v.end();
}

Json meta()
{
    return {{mcp::kMetaVersion, mcp::kModernVersion},
        {"io.modelcontextprotocol/clientCapabilities", Json::object()}};
}

// Notifications one MCP client has been sent.
struct Inbox {
    std::mutex m;
    std::vector<Json> messages;
    mcp::Send send()
    {
        return [this](const Json& message) {
            std::lock_guard lock(m);
            messages.push_back(message);
        };
    }
    std::vector<Json> take()
    {
        std::lock_guard lock(m);
        return std::exchange(messages, {});
    }
};

} // namespace

TEST(ApiHost, HostCommandsAreInBothCatalogsAndDispatchOnTheEmulationThread)
{
    FakeHost host;
    Session s(hostConfig(host));
    EXPECT_EQ(s.mode(), "gui");
    const auto catalog = names(ok(s, "api.catalog")["commands"]);
    EXPECT_TRUE(contains(catalog, "gui.test_echo"));
    EXPECT_TRUE(contains(catalog, "gui.test_toggle"));

    mcp::McpServer server(s);
    mcp::Connection connection;
    const Json list = server.handle({{"jsonrpc", "2.0"}, {"id", 1}, {"method", "tools/list"},
                                        {"params", {{"_meta", meta()}}}},
        connection, nullptr, nullptr).response;
    EXPECT_EQ(names(list["result"]["tools"]), catalog); // the same catalog (SC-008)

    const Json r = ok(s, "gui.test_echo", {{"text", "hi"}});
    EXPECT_EQ(r["text"], "hi");
    EXPECT_TRUE(r["onEmulationThread"]);
    fails(s, "gui.test_echo", Json::object(), "InvalidArguments");
}

TEST(ApiHost, CatalogAndStateChangesAreEvents)
{
    FakeHost host;
    Session s(hostConfig(host));
    std::atomic<int> catalogEvents{0};
    std::atomic<int> stateEvents{0};
    s.subscribe([&](const Event& e) {
        if (e.type == "catalog_changed") ++catalogEvents;
        if (e.type == "state_changed") ++stateEvents;
    });
    ok(s, "core.load_rom", {{"path", "instr_test-v5/rom_singles/01-basics.nes"}});
    EXPECT_EQ(stateEvents.load(), 1);
    EXPECT_TRUE(ok(s, "gui.test_toggle")["present"]);
    EXPECT_EQ(catalogEvents.load(), 1);
    EXPECT_TRUE(contains(s.registry().names(), "gui.test_extra"));
    ok(s, "gui.test_extra");
    EXPECT_FALSE(ok(s, "gui.test_toggle")["present"]);
    EXPECT_EQ(catalogEvents.load(), 2);
    EXPECT_FALSE(contains(s.registry().names(), "gui.test_extra"));
    fails(s, "gui.test_extra", Json::object(), "UnknownCommand");
    // The host hears the session's events too.
    EXPECT_TRUE(host.heard("state_changed"));
    EXPECT_TRUE(host.heard("catalog_changed"));
}

TEST(ApiHost, CatalogChangesNotifyMcpClientsOfBothRevisions)
{
    FakeHost host;
    Session s(hostConfig(host));
    mcp::McpServer server(s);

    // 2026-07-28: subscriptions/listen with toolsListChanged.
    Inbox modern;
    mcp::Connection modernConnection;
    const mcp::Reply listen = server.handle({{"jsonrpc", "2.0"}, {"id", "sub-1"},
                                                {"method", "subscriptions/listen"},
                                                {"params", {{"_meta", meta()},
                                                    {"notifications", {{"toolsListChanged", true}}}}}},
        modernConnection, modern.send(), nullptr);
    ASSERT_EQ(listen.kind, mcp::Reply::Kind::Subscription);
    ASSERT_EQ(modern.take().size(), 1u); // the acknowledgement

    // A subscription that did not ask for list changes hears none.
    Inbox quiet;
    const mcp::Reply other = server.handle({{"jsonrpc", "2.0"}, {"id", "sub-2"},
                                               {"method", "subscriptions/listen"},
                                               {"params", {{"_meta", meta()},
                                                   {"notifications", Json::object()}}}},
        modernConnection, quiet.send(), nullptr);
    ASSERT_EQ(quiet.take().size(), 1u);

    // 2025-11-25: initialize on a connection that can carry notifications
    // (stdio), then notifications/tools/list_changed without a subscription.
    Inbox legacy;
    mcp::Connection legacyConnection;
    legacyConnection.canNotify = true;
    server.handle({{"jsonrpc", "2.0"}, {"id", 0}, {"method", "initialize"},
                      {"params", {{"protocolVersion", mcp::kLegacyVersion},
                          {"capabilities", Json::object()},
                          {"clientInfo", {{"name", "t"}, {"version", "1"}}}}}},
        legacyConnection, legacy.send(), nullptr);

    ok(s, "gui.test_toggle"); // adds gui.test_extra
    const auto modernHeard = modern.take();
    ASSERT_EQ(modernHeard.size(), 1u);
    EXPECT_EQ(modernHeard[0]["method"], "notifications/tools/list_changed");
    EXPECT_EQ(modernHeard[0]["params"]["_meta"][mcp::kMetaSubscription], "sub-1");
    const auto legacyHeard = legacy.take();
    ASSERT_EQ(legacyHeard.size(), 1u);
    EXPECT_EQ(legacyHeard[0]["method"], "notifications/tools/list_changed");
    EXPECT_FALSE(legacyHeard[0]["params"].contains("_meta"));
    EXPECT_TRUE(quiet.take().empty());

    const Json list = server.handle({{"jsonrpc", "2.0"}, {"id", 2}, {"method", "tools/list"},
                                        {"params", {{"_meta", meta()}}}},
        modernConnection, nullptr, nullptr).response;
    EXPECT_TRUE(contains(names(list["result"]["tools"]), "gui.test_extra"));

    // A closed subscription hears nothing more; the open ones hear the removal.
    listen.subscription->close();
    ok(s, "gui.test_toggle"); // removes it
    EXPECT_TRUE(modern.take().empty());
    EXPECT_EQ(legacy.take().size(), 1u);
}
