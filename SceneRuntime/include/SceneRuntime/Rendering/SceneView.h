#pragma once
#include <SceneRuntime/Scene/SceneWorld.h>
#include <Engine/Graphics/Camera.h>

namespace SceneRuntime
{
    class SceneView final
    {
    public:
        static const ScenePlacement* CameraObject(const SceneLayout& layout);
        /// <summary>
        /// 親のスケールは位置に影響します。向きには祖先の回転だけが影響します。
        /// </summary>
        static bool Camera(const SceneWorld& world, float aspect, double seconds, Engine::Camera& camera);
        static Engine::DirectionalLight Light(const SceneWorld& world);
    };
}
