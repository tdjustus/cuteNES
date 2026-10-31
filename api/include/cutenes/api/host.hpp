#pragma once

#include <cutenes/api/command.hpp>

#include <cstdint>
#include <string>

namespace cutenes::api {

class Registry;

// A session event (contracts/transports.md "Events").
struct Event {
    // state_changed, run_state_changed, frame (at most 10 per second),
    // catalog_changed
    std::string type;
    std::uint64_t frame = 0;
    std::uint64_t masterCycle = 0;
    Json data = Json::object();
};

// The host extension point (spec FR-023, research R7). The headless server
// registers nothing; the GUI registers one gui.* command per user-facing
// action. Both calls happen on the session's emulation thread.
class IHost {
public:
    virtual ~IHost() = default;
    virtual std::string mode() const = 0;              // "headless" or "gui"
    virtual void registerCommands(Registry& registry) = 0;
    virtual void onSessionEvent(const Event& event) = 0;
};

class HeadlessHost final : public IHost {
public:
    std::string mode() const override { return "headless"; }
    void registerCommands(Registry&) override {}
    void onSessionEvent(const Event&) override {}
};

} // namespace cutenes::api
