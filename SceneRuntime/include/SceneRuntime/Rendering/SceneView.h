#pragma once
#include <SceneRuntime/Rendering/RenderPacket.h>
#include <Engine/Graphics/Camera.h>
#include <Engine/Graphics/Materials/DirectionalLight.h>

namespace SceneRuntime
{
    class SceneView final
    {
    public:
        /// <summary>不変の表示姿勢と主カメラ設定から、指定したviewのカメラを作ります。</summary>
        static bool Camera(const RenderPacket& packet, float aspect, double seconds, Engine::Camera& camera);
        /// <summary>不変の光源・霧と表示姿勢を使い、保存配置を参照せず照明を作ります。</summary>
        static Engine::DirectionalLight Light(const RenderPacket& packet);
    };
}
