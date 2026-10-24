#include <SceneRuntime/Scene/SceneAssetReferences.h>
#include <Engine/Assets/AssetReferenceResolver.h>
#include "SceneAssetWalk.h"
using namespace SceneRuntime::Detail;

namespace
{
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

    void ResolveSceneAssetReferences(SceneLayout& layout, const Engine::AssetDatabase& index, bool capture,
                                     bool resolve)
    {
        Resolve(layout, index, capture, resolve, 0);
    }
}
