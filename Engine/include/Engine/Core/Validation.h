#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <initializer_list>
#include <string_view>

namespace Engine
{
    inline bool InRange(double value, double minimum, double maximum)
    {
        return std::isfinite(value) && value >= minimum && value <= maximum;
    }
    inline bool PositiveRange(double value, double maximum)
    {
        return InRange(value, 0, maximum) && value > 0;
    }
    inline bool ValidString(std::string_view value, size_t maximum, bool required = false)
    {
        return value.size() <= maximum && value.find('\0') == std::string_view::npos && (!required || !value.empty());
    }
    inline bool OneOf(std::string_view value, std::initializer_list<std::string_view> choices)
    {
        return std::find(choices.begin(), choices.end(), value) != choices.end();
    }
}
