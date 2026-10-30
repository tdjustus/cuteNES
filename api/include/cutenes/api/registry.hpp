#pragma once

#include <cutenes/api/command.hpp>

#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace cutenes::api {

// Surface version (research R9): MINOR for added commands or optional
// fields, MAJOR for removals, renames and changed meanings. The golden
// catalog test (tests/api/catalog.golden.json) fails on any schema change,
// so a change cannot land without a deliberate bump and golden update.
inline constexpr int kSurfaceMajor = 0;
inline constexpr int kSurfaceMinor = 1;
std::string surfaceVersion();

// The ordered set of commands available in this mode. Iteration order is
// by name, so catalogs are deterministic (MCP tools/list ordering).
class Registry {
public:
    // Returns false (and changes nothing) if the name is taken or malformed.
    // Adds the frame/masterCycle fields every result carries to the output
    // schema.
    bool add(Command command);
    bool remove(const std::string& name);

    // A copy, so a command removed meanwhile stays callable to completion.
    std::optional<Command> find(const std::string& name) const;
    std::vector<std::string> names() const;
    // {name, description, inputSchema, outputSchema} for every command.
    Json catalog() const;

    // Called after add/remove succeeds (the session turns it into a
    // catalog_changed event).
    void setOnChange(std::function<void()> callback);

private:
    mutable std::mutex m_mutex;
    std::map<std::string, Command> m_commands;
    std::function<void()> m_onChange;
};

// Command names: lowercase namespace and verb_object, e.g. core.run_frames.
bool isValidCommandName(const std::string& name);

} // namespace cutenes::api
