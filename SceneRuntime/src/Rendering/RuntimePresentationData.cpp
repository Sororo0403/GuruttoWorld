#include "RuntimePresentationData.h"
#include <Engine/Core/Profiler.h>
#include <unordered_set>

namespace SceneRuntime
{
    std::shared_ptr<const SceneLayout> RuntimePresentationData::Extract(WorldState::ReadView view)
    {
        Engine::CpuScope scope("Presentation definition extraction");
        auto result = std::make_shared<SceneLayout>();
        result->settings = view.Definition()->Source().settings;
        std::unordered_set<std::uint32_t> selected;
        for (const auto entity : view.With<SkyComponent>())
            selected.insert(entity.slot);
        for (const auto entity : view.With<ParticleEmitterComponent>())
            selected.insert(entity.slot);
        for (const auto entity : view.With<CanvasComponent>())
            selected.insert(entity.slot);
        for (const auto entity : view.With<RectTransformComponent>())
            selected.insert(entity.slot);
        for (const auto entity : view.With<ImageComponent>())
            selected.insert(entity.slot);
        for (const auto entity : view.With<TextComponent>())
            selected.insert(entity.slot);
        for (const auto entity : view.With<ButtonComponent>())
            selected.insert(entity.slot);
        for (const auto entity : view.With<InputFieldComponent>())
            selected.insert(entity.slot);
        for (const auto entity : view.With<SliderComponent>())
            selected.insert(entity.slot);
        for (const auto entity : view.With<ToggleComponent>())
            selected.insert(entity.slot);
        for (const auto entity : view.With<ScrollViewComponent>())
            selected.insert(entity.slot);
        for (const auto entity : view.With<MaskComponent>())
            selected.insert(entity.slot);
        for (const auto entity : view.With<LayoutGroupComponent>())
            selected.insert(entity.slot);
        for (const auto entity : view.Entities())
        {
            if (!selected.contains(entity.slot))
                continue;
            ScenePlacement object;
            object.id = *view.ObjectId(entity);
            if (const auto parent = view.Parent(entity); parent)
                object.parentId = *view.ObjectId(*parent);
            const auto transform = *view.Transform(entity);
            object.position = transform.position;
            object.rotation = transform.rotation;
            object.scale = transform.scale;
            object.sky = view.Component<SkyComponent>(entity);
            object.particleEmitter = view.Component<ParticleEmitterComponent>(entity);
            object.canvas = view.Component<CanvasComponent>(entity);
            object.rectTransform = view.Component<RectTransformComponent>(entity);
            object.image = view.Component<ImageComponent>(entity);
            object.text = view.Component<TextComponent>(entity);
            object.button = view.Component<ButtonComponent>(entity);
            object.inputField = view.Component<InputFieldComponent>(entity);
            object.slider = view.Component<SliderComponent>(entity);
            object.toggle = view.Component<ToggleComponent>(entity);
            object.scrollView = view.Component<ScrollViewComponent>(entity);
            object.mask = view.Component<MaskComponent>(entity);
            object.layoutGroup = view.Component<LayoutGroupComponent>(entity);
            result->objects.push_back(std::move(object));
        }
        return result;
    }
    bool RuntimePresentationData::Changed(const SceneChangeSet& changes)
    {
        if (changes.structureChanged || !changes.objects.empty())
            return true;
        for (const auto& field : changes.fields)
            if (field.objectId.empty() && !field.field.starts_with("runtime."))
                return true;
        for (const auto& component : changes.components)
            if (component.componentType == "sky" || component.componentType == "particleEmitter" ||
                component.componentType == "canvas" || component.componentType == "rectTransform" ||
                component.componentType == "image" || component.componentType == "text" ||
                component.componentType == "button" || component.componentType == "inputField" ||
                component.componentType == "slider" || component.componentType == "toggle" ||
                component.componentType == "scrollView" || component.componentType == "mask" ||
                component.componentType == "layoutGroup")
                return true;
        return false;
    }
}
