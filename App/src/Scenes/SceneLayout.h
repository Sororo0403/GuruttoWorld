#pragma once
#include <array>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace App
{
    struct ScenePlacement
    {
        std::string id;
        std::string name;
        std::filesystem::path model; // アプリルートからの相対パス。
        std::array<float, 3> position{};
        std::array<float, 3> rotation{}; // XYZ、ラジアン。
        std::array<float, 3> scale{ 1.0f, 1.0f, 1.0f };
    };

    struct SceneLayout
    {
        std::vector<ScenePlacement> objects;
        // 読み込み・検証に失敗した場合は例外。呼び出し元の配置は変更しません。
        static SceneLayout Parse(std::string_view json);
        static SceneLayout Load(const std::filesystem::path& path);
    };
}
