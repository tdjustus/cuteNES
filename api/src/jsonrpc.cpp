#include <cutenes/api/jsonrpc.hpp>

#include <map>

namespace cutenes::api::jsonrpc {

namespace {

// Never throws on invalid UTF-8 (a file name, say): it is replaced.
std::string text(const Json& j)
{
    return j.dump(-1, ' ', false, Json::error_handler_t::replace);
}

Json response(const Json& id, Json errorOrResult, bool isError)
{
    Json r = {{"jsonrpc", "2.0"}, {"id", id}};
    r[isError ? "error" : "result"] = std::move(errorOrResult);
    return r;
}

Json protocolError(const Json& id, int code, const std::string& message)
{
    return response(id, {{"code", code}, {"message", message}}, true);
}

// One element: nullopt for a notification.
std::optional<Json> handleOne(const Json& request, const Executor& execute)
{
    // An id is a string, a number or null (JSON-RPC 2.0 section 4).
    const bool hasId = request.is_object() && request.contains("id");
    Json id = hasId ? request["id"] : Json();
    if (hasId && !(id.is_string() || id.is_number() || id.is_null())) {
        return protocolError(Json(), kInvalidRequest, "id must be a string, number or null");
    }
    if (!request.is_object() || request.value("jsonrpc", Json()) != "2.0"
        || !request.contains("method") || !request["method"].is_string()) {
        return protocolError(id, kInvalidRequest, "not a JSON-RPC 2.0 request");
    }
    if (request.contains("params") && !request["params"].is_object()) {
        // By-name parameters only: every command takes an arguments object.
        if (!hasId) {
            return std::nullopt;
        }
        return protocolError(id, kInvalidParams, "params must be an object");
    }
    const Result result = execute(request["method"].get<std::string>(),
        request.value("params", Json::object()));
    if (!hasId) {
        return std::nullopt;
    }
    if (result.isError()) {
        return response(id, errorObject(*result.error), true);
    }
    return response(id, result.value, false);
}

} // namespace

int errorCode(const std::string& name)
{
    static const std::map<std::string, int> codes = {
        {"UnknownCommand", kMethodNotFound},
        {"InvalidArguments", kInvalidParams},
        {"InternalError", kInternalError},
        {"NoCartridge", -32001},
        {"LoadError", -32002},
        {"LoadStateError", -32003},
        {"WriteError", -32004},
        {"PathOutsideLibrary", -32005},
        {"LimitExceeded", -32006},
        {"UnknownHandle", -32007},
        {"Cancelled", -32008},
        {"ShuttingDown", -32009},
        {"NotFound", -32010},
        {"NoBattery", -32011},
        {"NotImplemented", -32012},
    };
    const auto it = codes.find(name);
    return it == codes.end() ? -32000 : it->second;
}

Json errorObject(const Error& error)
{
    Json data = error.data.is_object() ? error.data : Json::object();
    data["name"] = error.name;
    return {{"code", errorCode(error.name)}, {"message", error.message}, {"data", data}};
}

std::optional<std::string> handle(const std::string& body, const Executor& execute)
{
    const Json parsed = Json::parse(body, nullptr, /*allow_exceptions=*/false);
    if (parsed.is_discarded()) {
        return text(protocolError(Json(), kParseError, "the body is not valid JSON"));
    }
    if (!parsed.is_array()) {
        const auto r = handleOne(parsed, execute);
        return r ? std::optional<std::string>(text(*r)) : std::nullopt;
    }
    if (parsed.empty()) {
        return text(protocolError(Json(), kInvalidRequest, "empty batch"));
    }
    Json responses = Json::array();
    for (const auto& request : parsed) {
        if (auto r = handleOne(request, execute)) {
            responses.push_back(std::move(*r));
        }
    }
    if (responses.empty()) {
        return std::nullopt;
    }
    return text(responses);
}

} // namespace cutenes::api::jsonrpc
