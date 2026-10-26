#include <SceneRuntime/Scene/ObjectReferenceDomain.h>

namespace SceneRuntime
{
    std::set<std::string> ObjectReferenceDomain(const SceneLayout& layout)
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
