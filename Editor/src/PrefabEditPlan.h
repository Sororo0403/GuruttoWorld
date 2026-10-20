#pragma once
#include "AssetFileChanges.h"
#include <SceneRuntime/Prefab.h>
#include <SceneRuntime/SceneAssetReferences.h>
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
                                       const std::string& property, const std::filesystem::path& root)
        {
            auto document = scene;
            document.ResolveAssets(root);
            const auto object = std::find_if(document.objects.begin(), document.objects.end(),
                                             [&](const auto& item)
                                             {
                                                 return item.id == objectId;
                                             });
            if (object == document.objects.end() || !object->prefab)
                throw std::runtime_error("Prefabの対象が見つかりません。");
            const auto relative = object->prefab->asset;
            if (!SceneRuntime::Prefab::ValidPath(relative))
                throw std::runtime_error("Prefabの保存先が不正です。");
            const auto path = std::filesystem::weakly_canonical(std::filesystem::absolute(root / relative));
            const auto before = Engine::FileContents::Read(path);
            if (!before)
                throw std::runtime_error("Prefabの元ファイルがありません。");
            std::string_view source(*before);
            if (source.starts_with("\xEF\xBB\xBF"))
                source.remove_prefix(3);
            auto asset = SceneRuntime::SceneLayout::Parse(source);
            asset.ResolveAssets(root);
            SceneRuntime::Prefab::Refresh(asset, root);
            SceneRuntime::Prefab::EditOverride(asset, document, objectId, property);
            PrefabEditPlan plan{document, {}};
            AddFiles(plan, path, relative, *before, asset);
            SceneRuntime::Prefab::Refresh(plan.scene, root, {{relative, asset}});
            ResolveSceneAssetReferences(plan.scene, Engine::AssetDatabase(root, true), true, false);
            plan.scene.Validate();
            return plan;
        }
        /// <summary>選択範囲の保存・元素材への適用・Variantの保存を、確定前の候補として準備します。</summary>
        static PrefabEditPlan Save(const SceneRuntime::SceneLayout& scene, const std::string& rootId,
                                   const std::filesystem::path& relative, const std::filesystem::path& root,
                                   bool overwrite, bool variant)
        {
            if (!SceneRuntime::Prefab::ValidPath(relative))
                throw std::runtime_error("Prefabの保存先が不正です。");
            const auto path = std::filesystem::weakly_canonical(std::filesystem::absolute(root / relative));
            const auto before = Engine::FileContents::Read(path);
            if (before && !overwrite)
                throw std::runtime_error("保存先のPrefabはすでに存在します。");
            auto document = scene;
            document.ResolveAssets(root);
            auto asset = variant ? SceneRuntime::Prefab::Variant(document, rootId)
                                 : SceneRuntime::Prefab::Extract(document, rootId);
            PrefabEditPlan plan{document, {}};
            AddFiles(plan, path, relative, before, asset);
            SceneRuntime::Prefab::Bind(plan.scene, rootId, relative);
            if (overwrite || variant)
                SceneRuntime::Prefab::Refresh(plan.scene, root, {{relative, asset}});
            ResolveSceneAssetReferences(plan.scene, Engine::AssetDatabase(root, true), true, false);
            plan.scene.Validate();
            return plan;
        }

    private:
        /// <summary>素材本体とIDメタデータを同じ変更セットへ収め、既存メタデータは生の内容を保持します。</summary>
        static void AddFiles(PrefabEditPlan& plan, const std::filesystem::path& path,
                             const std::filesystem::path& relative, std::optional<std::string> before,
                             const SceneRuntime::SceneLayout& asset)
        {
            auto change = AssetFileChanges::Prepare(path, std::move(before), asset.SerializeForSave(path));
            plan.scene.assetReferences[Engine::AssetDatabase::Text(relative)] = change.id;
            plan.files = std::move(change.files);
        }
    };
}
