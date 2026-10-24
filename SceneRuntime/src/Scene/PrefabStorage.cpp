#include <SceneRuntime/Scene/Prefab.h>
#include <Engine/Assets/AssetDatabase.h>
#include <Engine/Assets/ContentContext.h>

namespace SceneRuntime
{
    void Prefab::Refresh(SceneLayout& scene, const std::filesystem::path& assetsRoot)
    {
        Refresh(scene, assetsRoot, {});
    }
    void Prefab::Refresh(SceneLayout& scene, const std::filesystem::path& assetsRoot,
                         const std::map<std::filesystem::path, SceneLayout>& preparedAssets,
                         std::shared_ptr<const Engine::ContentContext> context)
    {
        if (!context)
            context = Engine::ContentContext::Capture(assetsRoot);
        context->RequireRoot(assetsRoot);
        std::optional<Engine::AssetDatabase> index;
        RefreshPrepared(scene,
                        [&](const std::filesystem::path& path)
                        {
                            auto source = preparedAssets.contains(path) ? preparedAssets.at(path)
                                                                        : SceneLayout::Load(assetsRoot / path);
                            if (!index)
                                index.emplace(context, true);
                            source.ResolveAssets(*index);
                            return source;
                        });
    }
    void Prefab::RevertInstance(SceneLayout& scene, const std::string& rootId, const std::filesystem::path& assetsRoot)
    {
        auto candidate = scene;
        Refresh(candidate, assetsRoot);
        RevertInstance(candidate, rootId);
        scene = std::move(candidate);
    }
    void Prefab::ApplyOverride(const SceneLayout& scene, const std::string& objectId, const std::string& property,
                               const std::filesystem::path& assetsRoot)
    {
        const auto found = std::find_if(scene.objects.begin(), scene.objects.end(),
                                        [&](const auto& item)
                                        {
                                            return item.id == objectId;
                                        });
        if (found == scene.objects.end())
            throw std::runtime_error("Prefab object missing: " + objectId);
        const auto& object = *found;
        if (!object.prefab)
            throw std::runtime_error("Object is not a prefab instance");
        const auto path = object.prefab->asset;
        auto asset = SceneLayout::Load(assetsRoot / path, assetsRoot);
        Refresh(asset, assetsRoot);
        EditOverride(asset, scene, objectId, property);
        asset.Save(assetsRoot / path);
    }
}
