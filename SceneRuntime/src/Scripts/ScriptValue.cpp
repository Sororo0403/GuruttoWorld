#include <SceneRuntime/Scripts/ScriptValue.h>

namespace SceneRuntime
{
    auto ScriptValue::Validate(size_t depth, size_t& remaining) const -> void
    {
        if (depth > 8 || remaining == 0)
            throw std::runtime_error("Script data exceeds nesting or element limit");
        --remaining;
        std::visit(
            [&](const auto& item)
            {
                ValidateItem(item, depth, remaining);
            },
            value);
    }

    auto ScriptValue::ValidateItem(const Array& item, size_t depth, size_t& remaining) -> void
    {
        if (item.size() > 256)
            throw std::runtime_error("Script data array is too large");
        for (const auto& child : item)
            child.Validate(depth + 1, remaining);
    }

    auto ScriptValue::ValidateItem(const Object& item, size_t depth, size_t& remaining) -> void
    {
        if (item.size() > 64)
            throw std::runtime_error("Script data object is too large");
        for (const auto& [name, child] : item)
        {
            if (name.empty() || name.size() > 128 || name.find('\0') != std::string::npos)
                throw std::runtime_error("Invalid script data field name");
            child.Validate(depth + 1, remaining);
        }
    }
}
