#include <SceneRuntime/Scripts/ScriptModuleApi.h>
#include <stdexcept>

bool Register_ChangeCaptureScript(void* context, SceneRuntime::RegisterModuleScript add)
{
    SceneRuntime::RuntimeScriptDefinition callbacks;
    callbacks.update = [](SceneRuntime::RuntimeScriptContext& script)
    {
        auto transform = script.Read().Transform(script.entity);
        if (!transform)
            throw std::runtime_error("DLL transform read failed");
        transform->position[0] += 2;
        const auto result = script.SetLocalTransform(*transform);
        const auto light = script.SetComponent(std::optional{SceneRuntime::PointLightComponent{}});
        const auto collider = script.SetComponent(std::optional{SceneRuntime::BoxColliderComponent{}});
        script.SetParent({});
        if (!light.succeeded || !collider.succeeded)
            throw std::runtime_error("DLL component capture failed");
        if (!result.succeeded || result.changes.transforms.size() != 1 ||
            !result.changes.transforms[0].Has(SceneRuntime::TransformField::Position))
            throw std::runtime_error("DLL transform capture failed");
    };
    return add(context, "CaptureTransformDll", &callbacks);
}
