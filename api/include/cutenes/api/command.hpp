#pragma once

// One API command (specs/002-ai-mcp-server/contracts/command-catalog.md).
// The public API and the MCP adapter both serve these, so the two surfaces
// cannot diverge (research R1).

#include <nlohmann/json.hpp>

#include <functional>
#include <optional>
#include <string>

namespace cutenes::api {

using Json = nlohmann::json;

// A structured failure (command-catalog.md "Errors"): `name` is the stable
// identifier clients branch on; `data` carries details such as the core
// enum name or the offending argument path.
struct Error {
    std::string name;
    std::string message;
    Json data = Json::object();
};

// Either a result value or an error, never both.
struct Result {
    Json value = Json::object();
    std::optional<Error> error;

    static Result ok(Json v = Json::object()) { return {std::move(v), std::nullopt}; }
    static Result fail(std::string name, std::string message,
        Json data = Json::object())
    {
        return {Json::object(), Error{std::move(name), std::move(message),
                                    std::move(data)}};
    }
    bool isError() const { return error.has_value(); }
};

class Session;

// Context a handler runs with: the session, plus the invocation's cancel
// check (polled between frames by long runs, research R5).
struct CallContext {
    Session& session;
    std::function<bool()> cancelled;
};

struct Command {
    std::string name;          // namespace.verb_object (research R8)
    std::string description;   // one line, shown to MCP clients
    Json inputSchema;          // JSON Schema 2020-12, object
    Json outputSchema;         // JSON Schema 2020-12, object
    bool mutating = false;     // changes emulation or session state
    bool needsRom = false;     // fails with NoCartridge when nothing is loaded
    std::function<Result(CallContext&, const Json& args)> handler;
};

} // namespace cutenes::api
