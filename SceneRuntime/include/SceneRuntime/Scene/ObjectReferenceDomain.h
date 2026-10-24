#pragma once
#include <SceneRuntime/Scene/SceneLayout.h>
#include <set>

namespace SceneRuntime
{
    /// <summary>未解決の参照も予約し、新しい個体が偶然その参照先にならないようにします。</summary>
    inline std::set<std::string> ObjectReferenceDomain(const SceneLayout& layout)
    {
        std::set<std::string> result;
        for (auto object : layout.objects)
        {
            result.insert(object.id);
            object.VisitObjectReferences(
                [&](const std::string& id)
                {
                    if (!id.empty())
                        result.insert(id);
                });
        }
        if (!layout.settings.mainCamera.empty())
            result.insert(layout.settings.mainCamera);
        return result;
    }
}
