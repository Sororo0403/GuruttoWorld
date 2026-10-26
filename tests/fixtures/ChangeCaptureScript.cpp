#include <SceneRuntime/Scripts/ScriptModuleApi.h>
#include <stdexcept>

bool Register_ChangeCaptureScript(void* context, SceneRuntime::RegisterModuleScript add)
{
    SceneRuntime::ScriptCallbacks callbacks;
    callbacks.update = [](SceneRuntime::ScriptContext& script)
    {
        auto transform = script.scene->Read().Transform(script.object.id);
        if (!transform)
            throw std::runtime_error("DLL transform read failed");
        transform->position[0] += 2;
        const auto result = script.SetLocalTransform(*transform);
        const auto light = script.SetComponent(&SceneRuntime::ScenePlacement::pointLight,
                                               std::optional{SceneRuntime::PointLightComponent{}});
        const auto collider = script.SetComponent(&SceneRuntime::ScenePlacement::boxCollider,
                                                  std::optional{SceneRuntime::BoxColliderComponent{}});
        script.SetParent("");
        if (!light.succeeded || !collider.succeeded)
            throw std::runtime_error("DLL component capture failed");
        if (!result.succeeded || result.changes.transforms.size() != 1 ||
            !result.changes.transforms[0].Has(SceneRuntime::TransformField::Position))
            throw std::runtime_error("DLL transform capture failed");
    };
    return add(context, "CaptureTransformDll", &callbacks);
}
