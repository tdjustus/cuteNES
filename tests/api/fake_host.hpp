#pragma once

// A stand-in for the GUI's host (FR-023, Q9): it registers gui.* commands
// the way the Qt application will, on the emulation thread, and records the
// session events it is told about.

#include <cutenes/api/registry.hpp>
#include <cutenes/api/schema.hpp>
#include <cutenes/api/session.hpp>

#include <algorithm>
#include <mutex>
#include <string>
#include <vector>

namespace cutenes::api::test {

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
            {"onEmulationThread", schema::boolean()}},
            {"text", "onEmulationThread"});
        echo.handler = [](CallContext& ctx, const Json& args) {
            // A host command may call other commands; they run inline.
            const Json version = ctx.session.call("api.version").value;
            return Result::ok({{"text", args["text"]},
                {"onEmulationThread", ctx.session.onEmulationThread() && version["mode"] == "gui"}});
        };
        registry.add(echo);

        // Adds gui.test_extra, or removes it: a catalog change on demand,
        // made on the emulation thread like any GUI change.
        Command toggle;
        toggle.name = "gui.test_toggle";
        toggle.description = "Add gui.test_extra, or remove it if it is there";
        toggle.inputSchema = schema::object({});
        toggle.outputSchema = schema::object({{"present", schema::boolean()}}, {"present"});
        toggle.mutating = true;
        toggle.handler = [](CallContext& ctx, const Json&) {
            Registry& r = ctx.session.registry();
            if (r.remove("gui.test_extra")) {
                return Result::ok({{"present", false}});
            }
            Command extra;
            extra.name = "gui.test_extra";
            extra.description = "Present while toggled on";
            extra.inputSchema = schema::object({});
            extra.outputSchema = schema::object({});
            extra.handler = [](CallContext&, const Json&) { return Result::ok(); };
            r.add(extra);
            return Result::ok({{"present", true}});
        };
        registry.add(toggle);
    }

    void onSessionEvent(const Event& e) override
    {
        std::lock_guard lock(m);
        events.push_back(e.type);
    }

    bool heard(const std::string& type)
    {
        std::lock_guard lock(m);
        return std::find(events.begin(), events.end(), type) != events.end();
    }

    std::mutex m;
    std::vector<std::string> events;
};

} // namespace cutenes::api::test
