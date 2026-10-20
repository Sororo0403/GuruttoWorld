#include <SceneRuntime/ScriptValueJson.h>
#include <Engine/Core/Validation.h>
#include <algorithm>
#include <iterator>

namespace SceneRuntime
{
    using Engine::Json;
    using Engine::JsonArray;
    using Engine::JsonObject;
    using Engine::JsonNumber;
    ScriptValue ReadScriptValue(const Json& source, size_t depth, size_t& remaining)
    {
        if (depth > 8 || remaining == 0)
            throw std::runtime_error("Script data exceeds nesting or element limit");
        --remaining;
        const auto& object = JsonObject(source);
        if (object.size() != 2)
            throw std::runtime_error("Invalid script data encoding");
        const auto kind = object.at("kind").get<std::string>();
        const auto& value = object.at("value");
        using Value = SceneRuntime::ScriptValue;
        if (kind == "number")
        {
            const auto number = JsonNumber(value);
            if (!Engine::InRange(number, -1000000, 1000000))
                throw std::runtime_error("Invalid script data number");
            return {static_cast<float>(number)};
        }
        if (kind == "bool")
            return {value.get<bool>()};
        if (kind == "string")
            return {value.get<std::string>()};
        if (kind == "reference")
            return {SceneRuntime::ScriptObjectReference{value.get<std::string>()}};
        if (kind == "array")
        {
            Value::Array result;
            const auto& children = JsonArray(value);
            std::transform(children.begin(), children.end(), std::back_inserter(result),
                           [&](const auto& child)
                           {
                               return ReadScriptValue(child, depth + 1, remaining);
                           });
            return {std::move(result)};
        }
        if (kind == "object")
        {
            Value::Object result;
            for (const auto& [key, child] : JsonObject(value).items())
                result.emplace(key, ReadScriptValue(child, depth + 1, remaining));
            return {std::move(result)};
        }
        throw std::runtime_error("Unknown script data kind");
    }
    Json WriteScriptValue(const SceneRuntime::ScriptValue& source)
    {
        return std::visit(
            [](const auto& value) -> Json
            {
                using T = std::decay_t<decltype(value)>;
                using Value = SceneRuntime::ScriptValue;
                if constexpr (std::is_same_v<T, float>)
                    return {{"kind", "number"}, {"value", value}};
                else if constexpr (std::is_same_v<T, bool>)
                    return {{"kind", "bool"}, {"value", value}};
                else if constexpr (std::is_same_v<T, std::string>)
                    return {{"kind", "string"}, {"value", value}};
                else if constexpr (std::is_same_v<T, SceneRuntime::ScriptObjectReference>)
                    return {{"kind", "reference"}, {"value", value.id}};
                else if constexpr (std::is_same_v<T, Value::Array>)
                {
                    Json array = Json::array();
                    std::transform(value.begin(), value.end(), std::back_inserter(array),
                                   [](const auto& child)
                                   {
                                       return WriteScriptValue(child);
                                   });
                    return {{"kind", "array"}, {"value", std::move(array)}};
                }
                else
                {
                    Json object = Json::object();
                    for (const auto& [key, child] : value)
                        object[key] = WriteScriptValue(child);
                    return {{"kind", "object"}, {"value", std::move(object)}};
                }
            },
            source.value);
    }
}
