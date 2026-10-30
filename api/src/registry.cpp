#include <cutenes/api/registry.hpp>

#include <algorithm>
#include <regex>

namespace cutenes::api {

std::string surfaceVersion()
{
    return std::to_string(kSurfaceMajor) + "." + std::to_string(kSurfaceMinor);
}

bool isValidCommandName(const std::string& name)
{
    static const std::regex re("^[a-z][a-z0-9]*\\.[a-z][a-z0-9]*(_[a-z0-9]+)*$");
    return std::regex_match(name, re);
}

namespace {

// Every result carries the instant it saw (command-catalog.md); the
// dispatcher adds the pair, so the registry adds it to every output schema.
void addInstantFields(Json& output)
{
    if (!output.is_object() || output.value("type", Json()) != "object") {
        output = {{"type", "object"}, {"properties", Json::object()},
            {"additionalProperties", false}};
    }
    if (!output.contains("properties")) {
        output["properties"] = Json::object();
        output["additionalProperties"] = false;
    }
    Json& required = output["required"];
    if (!required.is_array()) {
        required = Json::array();
    }
    for (const char* field : {"frame", "masterCycle"}) {
        output["properties"][field] = {{"type", "integer"}, {"minimum", 0}};
        if (std::find(required.begin(), required.end(), field) == required.end()) {
            required.push_back(field);
        }
    }
}

} // namespace

bool Registry::add(Command command)
{
    addInstantFields(command.outputSchema);
    std::function<void()> onChange;
    {
        std::lock_guard lock(m_mutex);
        if (!isValidCommandName(command.name) || !command.handler
            || m_commands.count(command.name)) {
            return false;
        }
        const std::string name = command.name;
        m_commands.emplace(name, std::move(command));
        onChange = m_onChange;
    }
    if (onChange) {
        onChange();
    }
    return true;
}

bool Registry::remove(const std::string& name)
{
    std::function<void()> onChange;
    {
        std::lock_guard lock(m_mutex);
        if (m_commands.erase(name) == 0) {
            return false;
        }
        onChange = m_onChange;
    }
    if (onChange) {
        onChange();
    }
    return true;
}

std::optional<Command> Registry::find(const std::string& name) const
{
    std::lock_guard lock(m_mutex);
    const auto it = m_commands.find(name);
    if (it == m_commands.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::vector<std::string> Registry::names() const
{
    std::lock_guard lock(m_mutex);
    std::vector<std::string> out;
    for (const auto& [name, command] : m_commands) {
        out.push_back(name);
    }
    return out;
}

Json Registry::catalog() const
{
    std::lock_guard lock(m_mutex);
    Json out = Json::array();
    for (const auto& [name, c] : m_commands) {
        out.push_back({{"name", c.name}, {"description", c.description},
            {"inputSchema", c.inputSchema}, {"outputSchema", c.outputSchema},
            {"mutating", c.mutating}, {"needsRom", c.needsRom}});
    }
    return out;
}

void Registry::setOnChange(std::function<void()> callback)
{
    std::lock_guard lock(m_mutex);
    m_onChange = std::move(callback);
}

} // namespace cutenes::api
