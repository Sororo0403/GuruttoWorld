#pragma once
#include <SceneRuntime/SceneLayout.h>
#include <Engine/Core/Json.h>
namespace SceneRuntime {
bool ReadUiComponent(const Engine::Json&, ScenePlacement&, const std::string&);
void WriteUiComponents(Engine::Json&, const ScenePlacement&);
}
