#include <SceneRuntime/Ui/UiState.h>
#include <Engine/Core/Validation.h>
#include <sstream>
#include <stdexcept>

namespace SceneRuntime
{
    float UiState::Value(const std::string& key, float fallback) const
    {
        const auto i = values.find(key);
        return i == values.end() ? fallback : i->second;
    }
    namespace
    {
        std::map<std::string, float> Assignments(const std::string& expression)
        {
            std::map<std::string, float> result;
            if (expression.empty())
                return result;
            if (expression.back() == '&')
                throw std::runtime_error("Empty state clause");
            std::istringstream input(expression);
            std::string clause;
            while (std::getline(input, clause, '&'))
            {
                const auto equal = clause.find('=');
                if (equal == std::string::npos || equal == 0 || clause.find('=', equal + 1) != std::string::npos)
                    throw std::runtime_error("Invalid state assignment");
                size_t used = 0;
                const auto value = std::stof(clause.substr(equal + 1), &used);
                if (used != clause.size() - equal - 1 || !Engine::InRange(value, -100000, 100000))
                    throw std::runtime_error("Invalid state value");
                result[clause.substr(0, equal)] = value;
            }
            return result;
        }
    }
    bool UiState::Matches(const std::string& expression) const
    {
        try
        {
            for (const auto& [key, value] : Assignments(expression))
                if (Value(key) != value)
                    return false;
            return true;
        }
        catch (...)
        {
            return false;
        }
    }
    bool UiState::Assign(const std::string& expression)
    {
        try
        {
            const auto assignments = Assignments(expression);
            for (const auto& [key, value] : assignments)
                values[key] = value;
            return true;
        }
        catch (...)
        {
            return false;
        }
    }
}
