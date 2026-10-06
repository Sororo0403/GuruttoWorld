#pragma once
// Pinned nlohmann/json 3.12.0; see Engine/externals/nlohmann/README.md.
#include "../../../externals/nlohmann/json.hpp"
#include <stdexcept>
namespace Engine {
using Json=nlohmann::json;
inline double JsonNumber(const Json& value) {
    if(!value.is_number()) throw std::runtime_error("JSON value must be a number");
    return value.get<double>();
}
inline const Json& JsonArray(const Json& value) {
    if(!value.is_array()) throw std::runtime_error("JSON value must be an array");
    return value;
}
inline const Json& JsonObject(const Json& value) {
    if(!value.is_object()) throw std::runtime_error("JSON value must be an object");
    return value;
}
}
