#pragma once
#include "../Assets/AssetFileChanges.h"
#include <SceneRuntime/Scene/Prefab.h>
#include <SceneRuntime/Scene/SceneAssetReferences.h>
#include <Engine/Assets/AssetDatabase.h>

namespace Editor
{
    struct PrefabEditPlan
    {
        SceneRuntime::SceneLayout scene;
        std::vector<FileChange> files;
    };
    class PrefabEdits final
    {
    public:
        /// <summary>素材の変更前内容から上書きと個体の更新を準備します。書き込みは行いません。</summary>
        static PrefabEditPlan Override(const SceneRuntime::SceneLayout& scene, const std::string& objectId,
                                       const std::string& property, const std::filesystem::path& root);
        /// <summary>選択範囲の保存・元素材への適用・Variantの保存を、確定前の候補として準備します。</summary>
        static PrefabEditPlan Save(const SceneRuntime::SceneLayout& scene, const std::string& rootId,
                                   const std::filesystem::path& relative, const std::filesystem::path& root,
                                   bool overwrite, bool variant);

    private:
        /// <summary>素材本体とIDメタデータを同じ変更セットへ収め、既存メタデータは生の内容を保持します。</summary>
        static void AddFiles(PrefabEditPlan& plan, const std::filesystem::path& path,
                             const std::filesystem::path& relative, std::optional<std::string> before,
                             const SceneRuntime::SceneLayout& asset);
    };
}
