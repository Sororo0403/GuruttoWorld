#include <SceneRuntime/SceneAssetReferences.h>
#include <Engine/Assets/AssetReferenceResolver.h>

namespace
{
    template <class Visitor, class Baseline>
    void Walk(SceneRuntime::SceneLayout& layout, Visitor&& visit, Baseline&& nested)
    {
        auto& settings = layout.settings;
        for (auto* shader :
             {&settings.skyShader, &settings.oceanShader, &settings.particleShader, &settings.postShader})
            visit(*shader);
        for (auto& object : layout.objects)
        {
            object.VisitAssetReferences(visit);
            if (object.prefab)
            {
                visit(object.prefab->asset);
                if (!object.prefab->baseline.empty())
                    nested(object.prefab->baseline);
            }
        }
    }
    void CheckDepth(size_t depth)
    {
        if (depth > 16)
            throw std::runtime_error("Prefab baseline nesting exceeds limit");
    }
    void Resolve(SceneRuntime::SceneLayout& layout, const Engine::AssetDatabase& index, bool capture, bool resolve,
                 size_t depth, std::map<std::string, std::string>* diagnostics = nullptr)
    {
        CheckDepth(depth);
        Engine::AssetReferenceResolver references(index, layout.assetReferences, resolve);
        std::map<std::string, std::string> unresolved;
        Walk(
            layout,
            [&](auto& path)
            {
                const auto before = path;
                try
                {
                    references.Visit(path);
                }
                catch (const std::exception& failure)
                {
                    if (!diagnostics)
                        throw;
                    path = before;
                    const auto key = [&]()
                    {
                        if constexpr (std::is_same_v<std::decay_t<decltype(path)>, std::string>)
                            return path;
                        else
                            return Engine::AssetDatabase::Text(path);
                    }();
                    (*diagnostics)[key] = failure.what();
                    if (layout.assetReferences.contains(key))
                        unresolved[key] = layout.assetReferences.at(key);
                }
            },
            [&](std::string& source)
            {
                auto baseline = SceneRuntime::SceneLayout::Parse(source);
                Resolve(baseline, index, capture, resolve, depth + 1, diagnostics);
                source = baseline.Serialize();
            });
        if (capture)
        {
            layout.assetReferences = references.Captured();
            layout.assetReferences.insert(unresolved.begin(), unresolved.end());
        }
    }

    void Inherit(SceneRuntime::SceneLayout& layout, const std::map<std::string, std::string>& known, size_t depth)
    {
        CheckDepth(depth);
        auto available = known;
        for (const auto& [path, id] : layout.assetReferences)
            available[path] = id;
        std::map<std::string, std::string> captured;
        Walk(
            layout,
            [&](const auto& value)
            {
                const auto path = [&]()
                {
                    if constexpr (std::is_same_v<std::decay_t<decltype(value)>, std::string>)
                        return Engine::AssetDatabase::Path(value);
                    else
                        return value;
                }();
                const auto key = Engine::AssetDatabase::Text(path);
                if (Engine::AssetDatabase::Valid(path) && available.contains(key))
                    captured[key] = available.at(key);
            },
            [&](std::string& source)
            {
                auto baseline = SceneRuntime::SceneLayout::Parse(source);
                Inherit(baseline, available, depth + 1);
                source = baseline.Serialize();
            });
        layout.assetReferences = std::move(captured);
    }

}
namespace SceneRuntime
{
    std::map<std::string, std::string> ResolveSceneAssetReferencesForEditing(SceneLayout& layout,
                                                                             const Engine::AssetDatabase& index)
    {
        std::map<std::string, std::string> diagnostics;
        Resolve(layout, index, true, true, 0, &diagnostics);
        return diagnostics;
    }
    void InheritSceneAssetReferences(SceneLayout& layout, const std::map<std::string, std::string>& known)
    {
        Inherit(layout, known, 0);
    }
    void ResolveSceneAssetReferences(SceneLayout& layout, const Engine::AssetDatabase& index, bool capture,
                                     bool resolve)
    {
        Resolve(layout, index, capture, resolve, 0);
    }
}
