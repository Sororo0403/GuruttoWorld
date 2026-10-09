#pragma once
#include <SceneRuntime/SceneLayout.h>
#include <Engine/Core/Json.h>
namespace SceneRuntime {
/// <summary>音響コンポーネントを検証して読み込みます。</summary>
bool ReadAudioComponent(const Engine::Json& object,ScenePlacement& placement,const std::string& type);
/// <summary>音響コンポーネントを書き出します。</summary>
void WriteAudioComponents(Engine::Json& array,const ScenePlacement& placement);
}
