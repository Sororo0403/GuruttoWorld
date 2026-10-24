#pragma once
#include <SceneRuntime/Scene/SceneLayout.h>
#include <Engine/Core/Json.h>

namespace SceneRuntime
{
    bool ReadEnvironmentComponent(const Engine::Json& object, ScenePlacement& placement, const std::string& type);
}
