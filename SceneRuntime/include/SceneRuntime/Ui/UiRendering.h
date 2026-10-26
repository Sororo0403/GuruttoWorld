#pragma once
#include <SceneRuntime/Ui/UiGeometry.h>
#include <SceneRuntime/Rendering/RenderPacket.h>
#include <Engine/Graphics/Renderers/SpriteRenderer.h>
#include <Engine/Graphics/Camera.h>

namespace SceneRuntime
{
    struct UiDrawResource
    {
        std::shared_ptr<Engine::SpriteRenderer> sprite, sceneSprite;
    };
    struct UiDrawResources final : RenderResource
    {
        std::map<std::string, UiDrawResource> parts;
    };
    class UiRendering final
    {
    public:
        /// <summary>指定viewへ不変のUI定義を描画し、使った資源をフレームの完了まで保持します。</summary>
        static bool Draw(ID3D12GraphicsCommandList* commands, const UiPacket& packet,
                         const std::shared_ptr<const UiDrawResources>& resources, unsigned int width,
                         unsigned int height, const UiState& state = {});
        /// <summary>Canvasの表示姿勢を使い、Worldを参照せずシーン内のUIを描画します。</summary>
        static bool DrawScene(ID3D12GraphicsCommandList* commands, const UiPacket& packet,
                              const std::shared_ptr<const UiDrawResources>& resources, const Engine::Camera& camera,
                              const UiState& state = {});
    };
}
