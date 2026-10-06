#pragma once
#include <SceneRuntime/SceneLayout.h>
#include <Engine/Core/Json.h>

namespace SceneRuntime
{
    SceneSettings ReadSceneSettings(const Engine::Json& object);
    Engine::Json WriteSceneSettings(const SceneSettings& settings);
}
