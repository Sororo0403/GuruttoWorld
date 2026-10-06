#pragma once
#include <SceneRuntime/SceneLayout.h>
#include <Engine/Core/Json.h>

namespace SceneRuntime
{
    bool ReadEnvironmentComponent(const Engine::Json& object,
        ScenePlacement& placement, const std::string& type);
    void WriteEnvironmentComponents(Engine::Json& array, const ScenePlacement& placement);
}
