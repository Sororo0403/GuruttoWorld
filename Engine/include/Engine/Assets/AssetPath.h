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
        static bool Valid(const std::filesystem::path& path);
        /// <summary>保存形式のアセットIDか確認します。</summary>
        static bool ValidId(const std::string& id);
    };
}
