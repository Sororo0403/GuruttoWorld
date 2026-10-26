#include "PrefabEditPlan.h"

namespace Editor
{
    auto PrefabEdits::Override(const SceneRuntime::SceneLayout& scene, const std::string& objectId,
                               const std::string& property, const std::filesystem::path& root) -> PrefabEditPlan
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

    auto PrefabEdits::Save(const SceneRuntime::SceneLayout& scene, const std::string& rootId,
                           const std::filesystem::path& relative, const std::filesystem::path& root, bool overwrite,
                           bool variant) -> PrefabEditPlan
    {
        if (!SceneRuntime::Prefab::ValidPath(relative))
            throw std::runtime_error("Prefabの保存先が不正です。");
        const auto path = std::filesystem::weakly_canonical(std::filesystem::absolute(root / relative));
        const auto before = Engine::FileContents::Read(path);
        if (before && !overwrite)
            throw std::runtime_error("保存先のPrefabはすでに存在します。");
        auto document = scene;
        document.ResolveAssets(root);
        auto asset =
            variant ? SceneRuntime::Prefab::Variant(document, rootId) : SceneRuntime::Prefab::Extract(document, rootId);
        PrefabEditPlan plan{document, {}};
        AddFiles(plan, path, relative, before, asset);
        SceneRuntime::Prefab::Bind(plan.scene, rootId, relative);
        if (overwrite || variant)
            SceneRuntime::Prefab::Refresh(plan.scene, root, {{relative, asset}});
        ResolveSceneAssetReferences(plan.scene, Engine::AssetDatabase(root, true), true, false);
        plan.scene.Validate();
        return plan;
    }

    auto PrefabEdits::AddFiles(PrefabEditPlan& plan, const std::filesystem::path& path,
                               const std::filesystem::path& relative, std::optional<std::string> before,
                               const SceneRuntime::SceneLayout& asset) -> void
    {
        auto change = AssetFileChanges::Prepare(path, std::move(before), asset.SerializeForSave(path));
        plan.scene.assetReferences[Engine::AssetDatabase::Text(relative)] = change.id;
        plan.files = std::move(change.files);
    }
}
