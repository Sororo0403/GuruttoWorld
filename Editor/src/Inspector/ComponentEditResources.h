#pragma once
#include "../Editing/EditState.h"

namespace Editor
{
    struct ComponentEditResources
    {
        static bool TextChanged(const SceneRuntime::ScenePlacement& before, const SceneRuntime::ScenePlacement& after)
        {
            if (before.text)
            {
                if (before.text->text != after.text->text || before.text->font != after.text->font ||
                    before.text->fontSize != after.text->fontSize)
                    return true;
                const std::array<float, 2> defaultSize{200, 60};
                const auto oldSize = before.rectTransform ? before.rectTransform->size : defaultSize;
                const auto newSize = after.rectTransform ? after.rectTransform->size : defaultSize;
                if (oldSize != newSize)
                    return true;
            }
            return false;
        }
        static bool Changed(const SceneRuntime::ScenePlacement& before, const SceneRuntime::ScenePlacement& after)
        {
            const auto resources = [](const SceneRuntime::ScenePlacement& value)
            {
                return std::tie(value.terrain, value.tilemap, value.inputField, value.meshRenderer, value.material,
                                value.animator);
            };
            if (resources(before) != resources(after))
                return true;
            const auto controls = [](const SceneRuntime::ScenePlacement& value)
            {
                return std::array{value.slider.has_value(), value.toggle.has_value(), value.image.has_value(),
                                  value.text.has_value()};
            };
            if (controls(before) != controls(after))
                return true;
            if (before.image && before.image->texture != after.image->texture)
                return true;
            if (TextChanged(before, after))
                return true;
            return false;
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
