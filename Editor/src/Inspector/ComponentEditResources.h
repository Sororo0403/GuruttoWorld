#pragma once
#include "../Editing/EditState.h"
#include <SceneRuntime/Rendering/SceneResourceChanges.h>

namespace Editor
{
    struct ComponentEditResources
    {
        /// <summary>資源交換の判定を、描画準備を所有するランタイムへ委譲します。</summary>
        static bool Changed(const SceneRuntime::ScenePlacement& before, const SceneRuntime::ScenePlacement& after)
        {
            return SceneRuntime::SceneResourceChanges::RequiresSynchronization(before, after);
        }
        static bool NeedsIdle(const SceneRuntime::SceneLayout& layout, const ObjectRequest& request)
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
    };
}
