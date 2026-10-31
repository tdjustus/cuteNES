#pragma once

// A JSON Schema 2020-12 validator for the keywords MCP's published
// schema.json uses: $ref into the document's $defs, allOf, anyOf, oneOf,
// const, enum, type, properties, required, additionalProperties, items,
// minimum, maximum and maxItems. Annotations (description, format,
// $schema) do not constrain. A keyword outside this set fails loudly
// instead of being skipped, so the validator cannot pass by ignoring it.

#include <nlohmann/json.hpp>

#include <cmath>
#include <optional>
#include <set>
#include <string>

namespace cutenes::mcp::test {

class SchemaValidator {
public:
    using Json = nlohmann::json;

    explicit SchemaValidator(Json document) : m_doc(std::move(document)) {}

    // nullopt when `instance` is valid against $defs/<definition>;
    // otherwise where and why it is not.
    std::optional<std::string> validate(const Json& instance, const std::string& definition) const
    {
        const auto& defs = m_doc.at("$defs");
        if (!defs.contains(definition)) {
            return "no definition " + definition;
        }
        return check(instance, defs.at(definition), "$");
    }

    bool has(const std::string& definition) const { return m_doc.at("$defs").contains(definition); }

private:
    static bool isType(const Json& v, const std::string& type)
    {
        if (type == "object") return v.is_object();
        if (type == "array") return v.is_array();
        if (type == "string") return v.is_string();
        if (type == "boolean") return v.is_boolean();
        if (type == "null") return v.is_null();
        if (type == "number") return v.is_number();
        if (type == "integer") {
            return v.is_number_integer()
                || (v.is_number_float() && std::floor(v.get<double>()) == v.get<double>());
        }
        return false;
    }

    std::optional<std::string> check(const Json& v, const Json& schema, const std::string& path) const
    {
        if (schema.is_boolean()) {
            return schema.get<bool>() ? std::nullopt : std::optional<std::string>(path + ": false schema");
        }
        static const std::set<std::string> annotations = {"description", "format", "$schema",
            "title", "default", "examples", "deprecated"};
        static const std::set<std::string> known = {"$ref", "allOf", "anyOf", "oneOf", "const",
            "enum", "type", "properties", "required", "additionalProperties", "items", "minimum",
            "maximum", "maxItems"};
        for (const auto& [key, value] : schema.items()) {
            if (!annotations.count(key) && !known.count(key)) {
                return path + ": the test validator does not implement " + key;
            }
        }
        if (schema.contains("$ref")) {
            const std::string ref = schema["$ref"];
            const std::string prefix = "#/$defs/";
            if (ref.rfind(prefix, 0) != 0) {
                return path + ": unsupported $ref " + ref;
            }
            if (auto e = check(v, m_doc.at("$defs").at(ref.substr(prefix.size())), path)) {
                return e;
            }
        }
        if (schema.contains("type")) {
            const Json& t = schema["type"];
            bool match = false;
            if (t.is_string()) {
                match = isType(v, t);
            } else {
                for (const auto& one : t) match = match || isType(v, one);
            }
            if (!match) {
                return path + ": not of type " + t.dump();
            }
        }
        if (schema.contains("const") && v != schema["const"]) {
            return path + ": not " + schema["const"].dump();
        }
        if (schema.contains("enum")) {
            bool found = false;
            for (const auto& e : schema["enum"]) found = found || v == e;
            if (!found) return path + ": not one of " + schema["enum"].dump();
        }
        if (schema.contains("allOf")) {
            for (const auto& sub : schema["allOf"]) {
                if (auto e = check(v, sub, path)) return e;
            }
        }
        if (schema.contains("anyOf") || schema.contains("oneOf")) {
            const bool any = schema.contains("anyOf");
            int matches = 0;
            std::string reasons;
            for (const auto& sub : schema[any ? "anyOf" : "oneOf"]) {
                if (auto e = check(v, sub, path)) {
                    reasons += "\n    " + *e;
                } else {
                    ++matches;
                }
            }
            if (matches == 0 || (!any && matches != 1)) {
                return path + ": matches " + std::to_string(matches) + " of "
                    + (any ? "anyOf" : "oneOf") + reasons;
            }
        }
        if (v.is_object()) {
            for (const auto& key : schema.value("required", Json::array())) {
                if (!v.contains(key.get<std::string>())) {
                    return path + ": missing " + key.get<std::string>();
                }
            }
            const Json props = schema.value("properties", Json::object());
            for (const auto& [key, value] : v.items()) {
                if (props.contains(key)) {
                    if (auto e = check(value, props[key], path + "." + key)) return e;
                } else if (schema.contains("additionalProperties")) {
                    if (auto e = check(value, schema["additionalProperties"], path + "." + key)) {
                        return e;
                    }
                }
            }
        }
        if (v.is_array()) {
            if (schema.contains("maxItems") && v.size() > schema["maxItems"].get<std::size_t>()) {
                return path + ": more than maxItems";
            }
            if (schema.contains("items")) {
                for (std::size_t i = 0; i < v.size(); ++i) {
                    if (auto e = check(v[i], schema["items"], path + "[" + std::to_string(i) + "]")) {
                        return e;
                    }
                }
            }
        }
        if (v.is_number()) {
            if (schema.contains("minimum") && v.get<double>() < schema["minimum"].get<double>()) {
                return path + ": below minimum";
            }
            if (schema.contains("maximum") && v.get<double>() > schema["maximum"].get<double>()) {
                return path + ": above maximum";
            }
        }
        return std::nullopt;
    }

    Json m_doc;
};

} // namespace cutenes::mcp::test
