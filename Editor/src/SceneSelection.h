#pragma once
#include "ObjectPanel.h"
#include <Engine/Graphics/Camera.h>

namespace Editor
{
    class SceneSelection final
    {
    public:
        static void Update(const SceneRuntime::SceneWorld& world, const Engine::Camera& camera,
            ObjectPanel& panel, bool active);
        static void Draw(const SceneRuntime::SceneWorld& world, const Engine::Camera& camera,
            const ObjectPanel& panel);
    };
}
