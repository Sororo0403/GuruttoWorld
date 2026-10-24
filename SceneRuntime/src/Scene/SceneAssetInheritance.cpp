#include <SceneRuntime/Scene/SceneAssetReferences.h>
#include <Engine/Assets/AssetPath.h>
#include "SceneAssetWalk.h"
using namespace SceneRuntime::Detail;

namespace
{
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
                        return Engine::AssetPath::Path(value);
                    else
                        return value;
                }();
                const auto key = Engine::AssetPath::Text(path);
                if (Engine::AssetPath::Valid(path) && available.contains(key))
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
    void InheritSceneAssetReferences(SceneLayout& layout, const std::map<std::string, std::string>& known)
    {
        Inherit(layout, known, 0);
    }
}
