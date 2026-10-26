#include <Engine/Core/Json.h>

namespace Engine
{
    double JsonNumber(const Json& value)
    {
        if (!value.is_number())
            throw std::runtime_error("JSON value must be a number");
        return value.get<double>();
    }

    const Json& JsonArray(const Json& value)
    {
        if (!value.is_array())
            throw std::runtime_error("JSON value must be an array");
        return value;
    }

    const Json& JsonObject(const Json& value)
    {
        if (!value.is_object())
            throw std::runtime_error("JSON value must be an object");
        return value;
    }
}
