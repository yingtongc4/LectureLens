#include "mcp/schema.hpp"

namespace ll::mcp {
namespace {

using json = nlohmann::json;

bool type_matches(const std::string& type, const json& v) {
    if (type == "string") return v.is_string();
    if (type == "integer") return v.is_number_integer() || (v.is_number_float() && v.get<double>() == static_cast<long long>(v.get<double>()));
    if (type == "number") return v.is_number();
    if (type == "boolean") return v.is_boolean();
    if (type == "object") return v.is_object();
    if (type == "array") return v.is_array();
    if (type == "null") return v.is_null();
    return true;  // unknown keyword: do not reject
}

std::optional<std::string> check_value(const std::string& name, const json& prop, const json& v) {
    if (prop.contains("type")) {
        const auto& t = prop["type"];
        bool ok = false;
        if (t.is_string()) {
            ok = type_matches(t.get<std::string>(), v);
        } else if (t.is_array()) {
            for (const auto& one : t) ok = ok || type_matches(one.get<std::string>(), v);
        }
        if (!ok) return "'" + name + "' must be of type " + t.dump();
    }
    if (prop.contains("enum")) {
        bool found = false;
        for (const auto& e : prop["enum"]) found = found || e == v;
        if (!found) return "'" + name + "' must be one of " + prop["enum"].dump();
    }
    if (v.is_number()) {
        const double x = v.get<double>();
        if (prop.contains("minimum") && x < prop["minimum"].get<double>()) {
            return "'" + name + "' must be >= " + prop["minimum"].dump();
        }
        if (prop.contains("maximum") && x > prop["maximum"].get<double>()) {
            return "'" + name + "' must be <= " + prop["maximum"].dump();
        }
    }
    if (v.is_string() && prop.contains("minLength") &&
        v.get<std::string>().size() < prop["minLength"].get<std::size_t>()) {
        return "'" + name + "' must have at least " + prop["minLength"].dump() + " characters";
    }
    return std::nullopt;
}

}  // namespace

std::optional<std::string> validate_arguments(const json& schema, const json& args) {
    if (!args.is_object()) return "arguments must be an object";
    const json props = schema.value("properties", json::object());
    for (const auto& r : schema.value("required", json::array())) {
        if (!args.contains(r.get<std::string>())) return "missing required argument '" + r.get<std::string>() + "'";
    }
    for (const auto& [name, v] : args.items()) {
        if (!props.contains(name)) {
            if (schema.value("additionalProperties", true) == false) return "unknown argument '" + name + "'";
            continue;
        }
        if (auto err = check_value(name, props[name], v)) return err;
    }
    return std::nullopt;
}

}  // namespace ll::mcp
