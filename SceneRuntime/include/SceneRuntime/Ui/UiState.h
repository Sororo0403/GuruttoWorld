#pragma once
#include <map>
#include <string>
#include <array>

namespace SceneRuntime
{
    struct UiState
    {
        std::map<std::string, float> values;
        std::map<std::string, bool> visibility;
        std::string hovered, pressed;
        std::map<std::string, std::string> strings;
        std::map<std::string, std::array<float, 2>> scrollOffsets;
        std::string focused;
        std::array<float, 2> dragPosition{}, dragScroll{};
        /// <summary>未設定時の既定値を指定して状態値を読み取ります。</summary>
        float Value(const std::string& key, float fallback = 0) const;
        /// <summary>条件式が現在の状態値に一致するか確認します。</summary>
        bool Matches(const std::string& expression) const;
        /// <summary>代入式全体を検証してから状態値に反映します。</summary>
        bool Assign(const std::string& expression);
    };
}
