#include <cutenes/api/schema.hpp>

#include "media.hpp"

#include <map>
#include <mutex>
#include <regex>
#include <set>

namespace cutenes::api {

namespace schema {

namespace {

Json describe(Json s, const std::string& description)
{
    if (!description.empty()) {
        s["description"] = description;
    }
    return s;
}

} // namespace

Json integer(std::int64_t min, std::int64_t max, std::string description)
{
    return describe({{"type", "integer"}, {"minimum", min}, {"maximum", max}},
        description);
}

Json u8(std::string description) { return integer(0, 0xFF, std::move(description)); }
Json u16(std::string description) { return integer(0, 0xFFFF, std::move(description)); }

Json u64(std::string description)
{
    return describe({{"type", "integer"}, {"minimum", 0}}, description);
}

Json boolean(std::string description)
{
    return describe({{"type", "boolean"}}, description);
}

Json string(std::string description)
{
    return describe({{"type", "string"}}, description);
}

Json stringEnum(std::vector<std::string> values, std::string description)
{
    return describe({{"type", "string"}, {"enum", values}}, description);
}

Json pattern(std::string regex, std::string description)
{
    return describe({{"type", "string"}, {"pattern", regex}}, description);
}

Json bytes(std::size_t maxBytes, std::string description)
{
    if (description.empty()) {
        description = "base64";
    }
    return describe({{"type", "string"}, {"contentEncoding", "base64"},
                        {"x-maxDecodedBytes", maxBytes}},
        description);
}

Json buttons(std::string description)
{
    return describe({{"type", "array"}, {"maxItems", 8}, {"items",
                        {{"type", "string"}, {"enum", {"A", "B", "Select",
                            "Start", "Up", "Down", "Left", "Right"}}}}},
        description);
}

Json array(Json items, std::size_t maxItems, std::string description)
{
    return describe({{"type", "array"}, {"items", std::move(items)},
                        {"maxItems", maxItems}},
        description);
}

Json object(std::initializer_list<std::pair<const char*, Json>> properties,
    std::vector<std::string> required, std::string description)
{
    Json props = Json::object();
    for (const auto& [name, s] : properties) {
        props[name] = s;
    }
    Json s = {{"type", "object"}, {"properties", props},
        {"additionalProperties", false}};
    if (!required.empty()) {
        s["required"] = required;
    }
    return describe(std::move(s), description);
}

} // namespace schema

namespace {

std::string escapePointer(const std::string& token)
{
    std::string out;
    for (char c : token) {
        if (c == '~') out += "~0";
        else if (c == '/') out += "~1";
        else out += c;
    }
    return out;
}

bool hasType(const Json& value, const std::string& type)
{
    if (type == "object") return value.is_object();
    if (type == "array") return value.is_array();
    if (type == "string") return value.is_string();
    if (type == "boolean") return value.is_boolean();
    if (type == "null") return value.is_null();
    if (type == "integer") return value.is_number_integer();
    if (type == "number") return value.is_number();
    return false;
}

// Patterns are compiled once; every call validates its arguments.
const std::regex& compiled(const std::string& pattern)
{
    static std::mutex mutex;
    static std::map<std::string, std::regex> cache;
    std::lock_guard lock(mutex);
    auto it = cache.find(pattern);
    if (it == cache.end()) {
        it = cache.emplace(pattern, std::regex(pattern)).first;
    }
    return it->second;
}

std::optional<SchemaFailure> fail(const std::string& path, std::string message)
{
    return SchemaFailure{path, std::move(message)};
}

std::optional<SchemaFailure> check(const Json& s, const Json& v, const std::string& path)
{
    if (s.contains("type")) {
        const Json& t = s["type"];
        bool ok = false;
        if (t.is_string()) {
            ok = hasType(v, t.get<std::string>());
        } else {
            for (const auto& each : t) {
                ok = ok || hasType(v, each.get<std::string>());
            }
        }
        if (!ok) {
            return fail(path, "expected type " + t.dump());
        }
    }
    if (s.contains("enum")) {
        bool found = false;
        for (const auto& e : s["enum"]) {
            found = found || e == v;
        }
        if (!found) {
            return fail(path, "must be one of " + s["enum"].dump());
        }
    }
    if (v.is_number()) {
        // Compare as signed or unsigned without converting through double.
        const auto below = [&](const Json& bound) {
            if (v.is_number_unsigned()) {
                return bound.get<std::int64_t>() > 0
                    && v.get<std::uint64_t>() < bound.get<std::uint64_t>();
            }
            return v.get<std::int64_t>() < bound.get<std::int64_t>();
        };
        const auto above = [&](const Json& bound) {
            if (v.is_number_unsigned()) {
                return bound.get<std::int64_t>() < 0
                    || v.get<std::uint64_t>() > bound.get<std::uint64_t>();
            }
            return v.get<std::int64_t>() > bound.get<std::int64_t>();
        };
        if (s.contains("minimum") && below(s["minimum"])) {
            return fail(path, "must be >= " + s["minimum"].dump());
        }
        if (s.contains("maximum") && above(s["maximum"])) {
            return fail(path, "must be <= " + s["maximum"].dump());
        }
    }
    if (v.is_string()) {
        const auto& text = v.get_ref<const std::string&>();
        if (s.contains("minLength") && text.size() < s["minLength"].get<std::size_t>()) {
            return fail(path, "shorter than " + s["minLength"].dump());
        }
        if (s.contains("maxLength") && text.size() > s["maxLength"].get<std::size_t>()) {
            return fail(path, "longer than " + s["maxLength"].dump());
        }
        if (s.contains("pattern")
            && !std::regex_search(text, compiled(s["pattern"].get<std::string>()))) {
            return fail(path, "does not match " + s["pattern"].get<std::string>());
        }
        if (s.value("contentEncoding", "") == "base64") {
            const auto decoded = media::base64Decode(text);
            if (!decoded) {
                return fail(path, "not valid base64");
            }
            if (s.contains("x-maxDecodedBytes")
                && decoded->size() > s["x-maxDecodedBytes"].get<std::size_t>()) {
                return fail(path, "more than " + s["x-maxDecodedBytes"].dump() + " bytes");
            }
        }
    }
    if (v.is_array()) {
        if (s.contains("minItems") && v.size() < s["minItems"].get<std::size_t>()) {
            return fail(path, "fewer than " + s["minItems"].dump() + " items");
        }
        if (s.contains("maxItems") && v.size() > s["maxItems"].get<std::size_t>()) {
            return fail(path, "more than " + s["maxItems"].dump() + " items");
        }
        if (s.contains("items")) {
            for (std::size_t i = 0; i < v.size(); ++i) {
                if (auto f = check(s["items"], v[i], path + "/" + std::to_string(i))) {
                    return f;
                }
            }
        }
    }
    if (v.is_object()) {
        const Json empty = Json::object();
        const Json& props = s.contains("properties") ? s["properties"] : empty;
        if (s.contains("required")) {
            for (const auto& name : s["required"]) {
                if (!v.contains(name.get<std::string>())) {
                    return fail(path + "/" + escapePointer(name.get<std::string>()),
                        "is required");
                }
            }
        }
        for (const auto& [key, item] : v.items()) {
            const std::string itemPath = path + "/" + escapePointer(key);
            if (props.contains(key)) {
                if (auto f = check(props[key], item, itemPath)) {
                    return f;
                }
            } else if (s.contains("additionalProperties")
                && s["additionalProperties"] == false) {
                return fail(itemPath, "is not an allowed property");
            }
        }
    }
    return std::nullopt;
}

const std::set<std::string>& knownKeywords()
{
    static const std::set<std::string> k = {"type", "properties", "required",
        "additionalProperties", "enum", "minimum", "maximum", "minLength",
        "maxLength", "pattern", "items", "minItems", "maxItems", "description",
        "contentEncoding", "x-maxDecodedBytes", "$schema"};
    return k;
}

std::optional<std::string> shape(const Json& s, const std::string& path)
{
    if (!s.is_object()) {
        return path + ": schema is not an object";
    }
    for (const auto& [key, value] : s.items()) {
        if (!knownKeywords().count(key)) {
            return path + ": unknown keyword " + key;
        }
    }
    if (s.contains("pattern")) {
        try {
            std::regex re(s["pattern"].get<std::string>());
        } catch (const std::regex_error&) {
            return path + ": invalid pattern";
        }
    }
    // A bare {"type": "object"} is a free-form value (catalog schemas);
    // anything with properties must be closed.
    if (s.value("type", Json()) == "object" && s.contains("properties")) {
        if (s.value("additionalProperties", Json()) != false) {
            return path + ": object is not closed";
        }
        const Json props = s.value("properties", Json::object());
        for (const auto& name : s.value("required", Json::array())) {
            if (!props.contains(name.get<std::string>())) {
                return path + ": required " + name.get<std::string>() + " is undefined";
            }
        }
        for (const auto& [key, sub] : props.items()) {
            if (auto e = shape(sub, path + "/" + key)) {
                return e;
            }
        }
    }
    if (s.contains("items")) {
        return shape(s["items"], path + "/items");
    }
    return std::nullopt;
}

} // namespace

std::optional<SchemaFailure> validate(const Json& schema, const Json& value)
{
    return check(schema, value, "");
}

std::optional<std::string> checkSchemaShape(const Json& schema)
{
    if (schema.value("type", Json()) != "object") {
        return std::string("top level is not an object schema");
    }
    return shape(schema, "");
}

} // namespace cutenes::api
