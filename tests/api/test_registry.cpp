// Registry and catalog (tasks A013, research R1/R8).

#include "support.hpp"

#include <cutenes/api/schema.hpp>

#include <set>

using namespace cutenes::api;
using namespace cutenes::api::test;

namespace {

Command echo(std::string name)
{
    Command c;
    c.name = std::move(name);
    c.description = "echo";
    c.inputSchema = schema::object({});
    c.outputSchema = schema::object({});
    c.handler = [](CallContext&, const Json&) { return Result::ok(); };
    return c;
}

} // namespace

TEST(ApiRegistry, NamesAreNamespacedVerbObject)
{
    EXPECT_TRUE(isValidCommandName("core.run_frames"));
    EXPECT_TRUE(isValidCommandName("api.version"));
    EXPECT_FALSE(isValidCommandName("run_frames"));
    EXPECT_FALSE(isValidCommandName("Core.run"));
    EXPECT_FALSE(isValidCommandName("core.run-frames"));
    EXPECT_FALSE(isValidCommandName("core.run__frames"));
    EXPECT_FALSE(isValidCommandName("core."));
}

TEST(ApiRegistry, RejectsDuplicatesAndKeepsNameOrder)
{
    Registry r;
    EXPECT_TRUE(r.add(echo("b.second")));
    EXPECT_TRUE(r.add(echo("a.first")));
    EXPECT_FALSE(r.add(echo("a.first")));
    EXPECT_FALSE(r.add(echo("bad name")));
    EXPECT_EQ(r.names(), (std::vector<std::string>{"a.first", "b.second"}));
    EXPECT_TRUE(r.remove("a.first"));
    EXPECT_FALSE(r.remove("a.first"));
    EXPECT_EQ(r.names(), (std::vector<std::string>{"b.second"}));
}

TEST(ApiRegistry, HeadlessCatalogHasOnlyCoreSessionAndApi)
{
    Session s(testConfig());
    for (const auto& name : s.registry().names()) {
        const auto ns = name.substr(0, name.find('.'));
        EXPECT_TRUE(ns == "core" || ns == "session" || ns == "api") << name;
    }
    EXPECT_EQ(s.mode(), "headless");
}

TEST(ApiRegistry, EverySchemaIsAClosedDraft2020Object)
{
    Session s(testConfig());
    for (const auto& c : s.registry().catalog()) {
        EXPECT_FALSE(c["description"].get<std::string>().empty()) << c["name"];
        if (auto e = checkSchemaShape(c["inputSchema"])) {
            ADD_FAILURE() << c["name"] << " input: " << *e;
        }
        if (auto e = checkSchemaShape(c["outputSchema"])) {
            ADD_FAILURE() << c["name"] << " output: " << *e;
        }
        // Every result carries the instant it saw (command-catalog.md).
        const auto required = c["outputSchema"]["required"];
        EXPECT_NE(std::find(required.begin(), required.end(), "frame"), required.end()) << c["name"];
        EXPECT_NE(std::find(required.begin(), required.end(), "masterCycle"), required.end())
            << c["name"];
    }
}

TEST(ApiRegistry, ApiCatalogMatchesTheRegistry)
{
    Session s(testConfig());
    const Json r = ok(s, "api.catalog");
    std::vector<std::string> names;
    for (const auto& c : r["commands"]) {
        names.push_back(c["name"]);
    }
    EXPECT_EQ(names, s.registry().names());
    EXPECT_EQ(r["surface"], surfaceVersion());
    EXPECT_EQ(ok(s, "api.version")["surface"], surfaceVersion());
}

TEST(ApiRegistry, UnknownCommandIsAStructuredError)
{
    Session s(testConfig());
    const Json data = fails(s, "core.fly", Json::object(), "UnknownCommand");
    EXPECT_EQ(data["command"], "core.fly");
}
