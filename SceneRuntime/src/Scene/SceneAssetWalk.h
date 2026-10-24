#pragma once
#include <SceneRuntime/Scene/SceneLayout.h>
#include <stdexcept>

namespace SceneRuntime::Detail
{
    /// <summary>配置・設定・入れ子の基準データに含まれるアセット参照を走査します。</summary>
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
    /// <summary>入れ子の基準データの深さを制限します。</summary>
    inline void CheckDepth(size_t depth)
    {
        if (depth > 16)
            throw std::runtime_error("Prefab baseline nesting exceeds limit");
    }
}
