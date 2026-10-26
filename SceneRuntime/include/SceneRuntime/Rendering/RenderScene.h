#pragma once
#include <SceneRuntime/Rendering/RenderPacket.h>
#include <SceneRuntime/Rendering/RenderTelemetry.h>
#include <SceneRuntime/Ui/UiState.h>
#include <Engine/Graphics/Camera.h>
#include <Engine/Graphics/Materials/DirectionalLight.h>
#include <Engine/Graphics/Renderers/OcclusionRenderer.h>

namespace SceneRuntime
{
    class RenderScene final
    {
    public:
        /// <summary>不変packetを描画し、記録開始から送信後の完了フェンスまで資源を保持します。</summary>
        static bool Draw(const std::shared_ptr<const RenderPacket>& packet, ID3D12GraphicsCommandList* commands,
                         const Engine::Camera& camera, const Engine::DirectionalLight& light, const UiState& state,
                         Engine::OcclusionRenderer& occlusion, MeshTelemetry& telemetry);
    };
}
