#pragma once
#include <array>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace SceneRuntime
{
    enum class TransformSpace { World, Local };

    struct ScenePlacement
    {
        std::string id;
        std::string name;
        std::filesystem::path model; // アプリルートからの相対パス。
        std::array<float, 3> position{};
        std::array<float, 3> rotation{}; // XYZ、ラジアン。
        std::array<float, 3> scale{ 1.0f, 1.0f, 1.0f };
        std::string parentId; // Empty means root. Coordinates follow SceneLayout::transformSpace.
    };

    struct SceneLayout
    {
        std::vector<ScenePlacement> objects;
        TransformSpace transformSpace = TransformSpace::World;
        // 読み込み・検証に失敗した場合は例外。呼び出し元の配置は変更しません。
        static SceneLayout Parse(std::string_view json);
        std::string Serialize() const;
        // With overwrite=false, the final atomic commit also refuses an existing destination.
        void Save(const std::filesystem::path& path, bool overwrite = true) const;
        static SceneLayout Load(const std::filesystem::path& path);
    };
}
