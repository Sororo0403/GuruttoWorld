#pragma once
#include <SceneRuntime/SceneLayout.h>
#include <Engine/Core/Json.h>

namespace SceneRuntime
{
    /// <summary>保存対象の型ごとに、単体の保存形式を構築します。</summary>
#define ENGINE_SCENE_WRITER(Type, member, references, assets, order)                                                   \
    Engine::Json WriteSceneComponent(const Type& component);
    ENGINE_SCENE_COMPONENTS(ENGINE_SCENE_WRITER, ENGINE_SCENE_WRITER)
#undef ENGINE_SCENE_WRITER
    void ReadSceneComponents(const Engine::Json& object, ScenePlacement& placement, bool legacy);
    Engine::Json WriteSceneComponents(const ScenePlacement& placement);
}
