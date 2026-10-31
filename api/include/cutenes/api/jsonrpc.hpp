#pragma once

// JSON-RPC 2.0 over the command catalog (contracts/transports.md "API
// protocol"): `method` is a command name, `params` its arguments object.

#include <cutenes/api/command.hpp>

#include <functional>
#include <optional>
#include <string>

namespace cutenes::api::jsonrpc {

// JSON-RPC 2.0 reserved codes.
inline constexpr int kParseError = -32700;
inline constexpr int kInvalidRequest = -32600;
inline constexpr int kMethodNotFound = -32601;
inline constexpr int kInvalidParams = -32602;
inline constexpr int kInternalError = -32603;

// The code for a command error name (command-catalog.md "Errors"):
// UnknownCommand and InvalidArguments use the JSON-RPC codes, the rest the
// implementation-defined range -32000 to -32019.
int errorCode(const std::string& name);

// {code, message, data: {name, ...error data}}.
Json errorObject(const Error& error);

// Runs one command; the HTTP layer adds cancellation and shutdown.
using Executor = std::function<Result(const std::string& method, const Json& params)>;

// Handles one request body: a request, a notification or a batch (run in
// array order, not atomically). Returns the response text, or nullopt when
// nothing is owed (only notifications).
std::optional<std::string> handle(const std::string& body, const Executor& execute);

} // namespace cutenes::api::jsonrpc
