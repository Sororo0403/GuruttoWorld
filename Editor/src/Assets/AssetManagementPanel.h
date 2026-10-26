#pragma once
#include "../Project/ProjectCatalog.h"
#include "AssetDependencies.h"
#include <Engine/Assets/AssetDatabase.h>
#include <functional>
#include <imgui.h>
namespace Editor
{
    class AssetManagementPanel final
    {
        std::filesystem::path selected_;
        Engine::AssetMetadata settings_;
        std::optional<std::string> settingsSource_;
        std::array<char, 1024> destination_{};
        std::string error_;
        AssetDependencies dependencies_;
        /// <summary>選択した素材の編集基準と移動先を読み込みます。</summary>
        void SelectAsset(const std::filesystem::path& root, const ProjectAsset& asset);
        /// <summary>モデルのインポート設定を編集します。</summary>
        void DrawModelImport(const std::filesystem::path& root, const ProjectAsset& asset);
        /// <summary>選択素材の参照先と索引の診断を表示します。</summary>
        void DrawDependencies(const std::filesystem::path& root, const ProjectAsset& asset);
        /// <summary>テクスチャの取り込み設定の編集画面を描画します。</summary>
        void DrawTextureImport(const std::filesystem::path& root, const ProjectAsset& asset);

    public:
        /// <summary>インポートと移動を編集し、移動前の検証を成功させてからファイルを変更します。</summary>
        std::optional<std::filesystem::path> Draw(
            const std::filesystem::path& root, const ProjectAsset& asset, bool enabled,
            const std::function<void(const std::filesystem::path&, const std::filesystem::path&)>& beforeMove = {});
    };
}
