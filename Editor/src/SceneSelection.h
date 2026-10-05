#pragma once
#include "EditState.h"
#include "SceneViewport.h"
#include <Engine/Graphics/Camera.h>

namespace Editor
{
    class SceneSelection final
    {
    public:
        static void Update(const SceneRuntime::SceneWorld& world, const Engine::Camera& camera,
            EditState& state, const SceneViewport& viewport, bool active);
        static void Draw(const SceneRuntime::SceneWorld& world, const Engine::Camera& camera,
            const EditState& state, const SceneViewport& viewport);
    };
}
