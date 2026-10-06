#pragma once
#include <SceneRuntime/SceneWorld.h>
#include <Engine/Graphics/Camera.h>

namespace SceneRuntime
{
    class SceneView final
    {
    public:
        static const ScenePlacement* CameraObject(const SceneLayout& layout);
        // Parent scale affects position; only ancestor rotations affect orientation.
        static bool Camera(const SceneWorld& world, float aspect, double seconds, Engine::Camera& camera);
        static Engine::DirectionalLight Light(const SceneWorld& world);
    };
}
