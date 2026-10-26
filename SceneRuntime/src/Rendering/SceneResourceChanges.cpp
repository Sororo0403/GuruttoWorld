#include <SceneRuntime/Rendering/SceneResourceChanges.h>

namespace SceneRuntime
{
    auto SceneResourceChanges::AnimationChanged(const ScenePlacement& before, const ScenePlacement& after) -> bool
    {
        return std::tie(before.animator, before.terrain, before.tilemap) !=
                   std::tie(after.animator, after.terrain, after.tilemap) ||
               (before.animator && MeshChanged(before, after));
    }

    auto SceneResourceChanges::UiChanged(const ScenePlacement& before, const ScenePlacement& after) -> bool
    {
        if (before.inputField != after.inputField || before.slider.has_value() != after.slider.has_value() ||
            before.toggle.has_value() != after.toggle.has_value() ||
            before.image.has_value() != after.image.has_value() || before.text.has_value() != after.text.has_value())
            return true;
        if (before.image && before.image->texture != after.image->texture)
            return true;
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
}
