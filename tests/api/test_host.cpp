// The host extension point (FR-023; the full Q9 lands in P4): a FakeHost's
// commands appear in the catalog, run on the emulation thread, and changes
// to them are catalog_changed events.

#include "support.hpp"

#include <cutenes/api/schema.hpp>

#include <atomic>
#include <mutex>

using namespace cutenes::api;
using namespace cutenes::api::test;

namespace {

class FakeHost final : public IHost {
public:
    std::string mode() const override { return "gui"; }
    void registerCommands(Registry& registry) override
    {
        Command echo;
        echo.name = "gui.test_echo";
        echo.description = "Echo the text back, from the emulation thread";
        echo.inputSchema = schema::object({{"text", schema::string()}}, {"text"});
        echo.outputSchema = schema::object({{"text", schema::string()},
            {"onEmulationThread", schema::boolean()}, {"frame", schema::u64()},
            {"masterCycle", schema::u64()}},
            {"text", "onEmulationThread", "frame", "masterCycle"});
        echo.handler = [](CallContext& ctx, const Json& args) {
            // A host command may call other commands; they run inline.
            const Json version = ctx.session.call("api.version").value;
            return Result::ok({{"text", args["text"]},
                {"onEmulationThread", ctx.session.onEmulationThread() && version["mode"] == "gui"}});
        };
        registry.add(echo);
    }
    void onSessionEvent(const Event& e) override
    {
        std::lock_guard lock(m);
        events.push_back(e.type);
    }
    std::mutex m;
    std::vector<std::string> events;
};

} // namespace

TEST(ApiHost, HostCommandsAreCatalogedAndDispatch)
{
    FakeHost host;
    SessionConfig c = testConfig();
    c.host = &host;
    Session s(c);
    EXPECT_EQ(s.mode(), "gui");
    const auto names = s.registry().names();
    EXPECT_NE(std::find(names.begin(), names.end(), "gui.test_echo"), names.end());
    const Json r = ok(s, "gui.test_echo", {{"text", "hi"}});
    EXPECT_EQ(r["text"], "hi");
    EXPECT_TRUE(r["onEmulationThread"]);
    fails(s, "gui.test_echo", Json::object(), "InvalidArguments");
}

TEST(ApiHost, CatalogAndStateChangesAreEvents)
{
    FakeHost host;
    SessionConfig c = testConfig();
    c.host = &host;
    Session s(c);
    std::atomic<int> catalogEvents{0};
    std::atomic<int> stateEvents{0};
    s.subscribe([&](const Event& e) {
        if (e.type == "catalog_changed") ++catalogEvents;
        if (e.type == "state_changed") ++stateEvents;
    });
    // Catalog edits happen on the emulation thread (host.hpp).
    Command extra;
    extra.name = "gui.test_toggle";
    extra.description = "toggle";
    extra.inputSchema = schema::object({});
    extra.outputSchema = schema::object({});
    extra.handler = [](CallContext&, const Json&) { return Result::ok(); };
    Command adder;
    adder.name = "gui.test_add";
    adder.description = "add gui.test_toggle";
    adder.inputSchema = schema::object({});
    adder.outputSchema = schema::object({});
    adder.handler = [extra](CallContext& ctx, const Json&) {
        ctx.session.registry().add(extra);
        return Result::ok();
    };
    s.call("api.version"); // after construction the host's registration is done
    ok(s, "core.load_rom", {{"path", "instr_test-v5/rom_singles/01-basics.nes"}});
    EXPECT_EQ(stateEvents.load(), 1);
    // Adding through a dispatched command keeps the change on the thread.
    s.registry().add(adder);
    EXPECT_GE(catalogEvents.load(), 1);
    ok(s, "gui.test_add");
    EXPECT_GE(catalogEvents.load(), 2);
    ok(s, "gui.test_toggle");
    std::lock_guard lock(host.m);
    EXPECT_NE(std::find(host.events.begin(), host.events.end(), "state_changed"), host.events.end());
}
