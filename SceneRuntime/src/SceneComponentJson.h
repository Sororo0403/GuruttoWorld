#pragma once
#include <SceneRuntime/SceneLayout.h>
#include <Engine/Core/Json.h>

namespace SceneRuntime
{
    void ReadSceneComponents(const Engine::Json& object,
        ScenePlacement& placement, bool legacy);
    Engine::Json WriteSceneComponents(const ScenePlacement& placement);
}
