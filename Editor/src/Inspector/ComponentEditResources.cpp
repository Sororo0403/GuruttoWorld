#include "ComponentEditResources.h"

namespace Editor
{
    auto ComponentEditResources::NeedsIdle(const SceneRuntime::SceneLayout& layout, const ObjectRequest& request)
        -> bool
    {
        if (request.action != ObjectAction::Components)
            return true;
        const auto owner = [&](const SceneRuntime::ScenePlacement& candidate)
        {
            const auto found = std::find_if(layout.objects.begin(), layout.objects.end(),
                                            [&](const auto& object)
                                            {
                                                return object.id == candidate.id;
                                            });
            return found == layout.objects.end() || Changed(*found, candidate);
        };
        if (!request.componentBatch.empty())
            return std::any_of(request.componentBatch.begin(), request.componentBatch.end(), owner);
        if (!request.components || request.components->id != request.id)
            return true;
        return owner(*request.components);
    }
}
