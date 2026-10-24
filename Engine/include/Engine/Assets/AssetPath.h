#pragma once
#include <filesystem>
#include <string>
#include <algorithm>

namespace Engine
{
    struct AssetPath final
    {
        /// <summary>論理アセットパスをUTF-8の保存値へ変換します。</summary>
        static std::string Text(const std::filesystem::path& path)
        {
            const auto s = path.generic_u8string();
            return {s.begin(), s.end()};
        }
        /// <summary>UTF-8の保存値を論理パスへ変換します。</summary>
        static std::filesystem::path Path(const std::string& value)
        {
            return std::filesystem::path(std::u8string(value.begin(), value.end()));
        }
        /// <summary>ルートからの逸脱を含まないアセットパスか確認します。</summary>
        static bool Valid(const std::filesystem::path& path)
        {
            const auto text = Text(path);
            return !path.is_absolute() && !path.has_root_name() &&
                   (text.starts_with("Assets/") || text.starts_with("Shaders/")) &&
                   text.find(':') == std::string::npos && text.find('\\') == std::string::npos &&
                   text.find('\0') == std::string::npos &&
                   std::none_of(path.begin(), path.end(),
                                [](const auto& part)
                                {
                                    return part == ".." || part == ".";
                                });
        }
        /// <summary>保存形式のアセットIDか確認します。</summary>
        static bool ValidId(const std::string& id)
        {
            return id.size() == 32 && std::all_of(id.begin(), id.end(),
                                                  [](char c)
                                                  {
                                                      return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
                                                  });
        }
    };
}
