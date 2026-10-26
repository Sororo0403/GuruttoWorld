#pragma once
#include "../Assets/AssetFileChanges.h"
#include "../Project/ProjectCatalog.h"
#include <SceneRuntime/Rendering/MaterialAsset.h>
#include <SceneRuntime/Scene/SceneLayout.h>

namespace Editor
{
    struct MaterialWorkflow
    {
        /// <summary>処理対象を取得します。</summary>
        static std::filesystem::path Target(const std::string& name);
        /// <summary>指定した対象を複製します。</summary>
        static void Duplicate(const std::filesystem::path& root, const SceneRuntime::MaterialAsset& asset,
                              const std::filesystem::path& destination);
        /// <summary>対象を割り当てます。</summary>
        static std::vector<SceneRuntime::ScenePlacement> Assign(const SceneRuntime::SceneLayout& layout,
                                                                const std::vector<std::string>& ids,
                                                                const std::filesystem::path& path);
    };
}
