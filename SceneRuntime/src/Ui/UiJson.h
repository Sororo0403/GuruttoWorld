#pragma once
#include <SceneRuntime/Scene/SceneLayout.h>
#include <Engine/Core/Json.h>
namespace SceneRuntime
{
    bool ReadUiComponent(const Engine::Json&, ScenePlacement&, const std::string&);
}
