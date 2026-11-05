#pragma once

#include <optional>
#include <string>

#include <nlohmann/json.hpp>

namespace ll::mcp {

// Checks tool arguments against the subset of JSON Schema that the tools here
// use: an object with `properties` (type, enum, minimum, maximum, minLength)
// `required` and `additionalProperties: false`. Returns a message for the
// first problem, or nullopt when the arguments are valid.
std::optional<std::string> validate_arguments(const nlohmann::json& schema, const nlohmann::json& args);

}  // namespace ll::mcp
