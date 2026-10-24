#pragma once
#include <SceneRuntime/Scene/SceneLayout.h>
#include <Engine/Core/Json.h>
namespace SceneRuntime
{
    /// <summary>地形・Tilemap・ナビゲーションを検証して読み込みます。</summary>
    bool ReadGenreComponent(const Engine::Json& object, ScenePlacement& placement, const std::string& type);
}
