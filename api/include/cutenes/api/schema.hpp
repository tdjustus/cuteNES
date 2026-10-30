#pragma once

// JSON Schema helpers: builders for command schemas, and the subset
// validator that checks arguments before a handler runs (tasks A023).

#include <cutenes/api/command.hpp>

#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace cutenes::api {

inline constexpr std::size_t kMaxBytesPerCall = 64 * 1024; // FR-012

namespace schema {

Json integer(std::int64_t min, std::int64_t max, std::string description = {});
Json u8(std::string description = {});
Json u16(std::string description = {});
Json u64(std::string description = {});
Json boolean(std::string description = {});
Json string(std::string description = {});
Json stringEnum(std::vector<std::string> values, std::string description = {});
Json pattern(std::string regex, std::string description = {});
// Base64 data of at most maxBytes decoded bytes.
Json bytes(std::size_t maxBytes = kMaxBytesPerCall, std::string description = {});
// An array of NES button names.
Json buttons(std::string description = {});
Json array(Json items, std::size_t maxItems, std::string description = {});
// A closed object (additionalProperties false).
Json object(std::initializer_list<std::pair<const char*, Json>> properties,
    std::vector<std::string> required = {}, std::string description = {});

} // namespace schema

// Validates `value` against the subset this API uses: type, properties,
// required, additionalProperties, enum, minimum, maximum, minLength,
// maxLength, pattern, items, minItems, maxItems, and the contentEncoding
// "base64" with x-maxDecodedBytes. Returns the first failure as
// {path, message}; path is a JSON Pointer such as "/script/3".
struct SchemaFailure {
    std::string path;
    std::string message;
};
std::optional<SchemaFailure> validate(const Json& schema, const Json& value);

// The structural rules a command schema must follow (registry tests):
// draft 2020-12 keywords only, objects closed, required names present.
std::optional<std::string> checkSchemaShape(const Json& schema);

} // namespace cutenes::api
