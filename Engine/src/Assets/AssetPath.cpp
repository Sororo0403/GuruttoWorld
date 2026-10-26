#include <Engine/Assets/AssetPath.h>

namespace Engine
{
    auto AssetPath::Valid(const std::filesystem::path& path) -> bool
    {
        const auto text = Text(path);
        return !path.is_absolute() && !path.has_root_name() &&
               (text.starts_with("Assets/") || text.starts_with("Shaders/") || text.starts_with("Engine/Shaders/")) &&
               text.find(':') == std::string::npos && text.find('\\') == std::string::npos &&
               text.find('\0') == std::string::npos &&
               std::none_of(path.begin(), path.end(),
                            [](const auto& part)
                            {
                                return part == ".." || part == ".";
                            });
    }

    auto AssetPath::ValidId(const std::string& id) -> bool
    {
        return id.size() == 32 && std::all_of(id.begin(), id.end(),
                                              [](char c)
                                              {
                                                  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
                                              });
    }
}
