#pragma once
#include <SceneRuntime/Rendering/RenderScene.h>
#include <SceneRuntime/Rendering/SceneView.h>
#include <SceneRuntime/Ui/UiRendering.h>
#include <Engine/Graphics/Renderers/ParticleRenderer.h>
#include <Engine/Graphics/Renderers/PostEffectRenderer.h>

namespace SceneRuntime
{
    struct SceneDrawOptions
    {
        const Engine::Camera* sceneCamera = nullptr;
        double seconds = 0;
        bool motionEnabled = true;
        const UiState* uiState = nullptr;
        bool showSceneUi = true;
        bool showGameUi = true;
    };
    struct PresentationResources final : RenderResource
    {
        Microsoft::WRL::ComPtr<ID3D12Device> device;
        std::shared_ptr<Engine::SpriteRenderer> sky, ocean;
        std::shared_ptr<Engine::ParticleRenderer> particles;
        std::shared_ptr<Engine::PostEffectRenderer> postEffects;
        std::map<std::filesystem::path, std::shared_ptr<Engine::SpriteRenderer>> customSky, customOcean;
        std::map<std::filesystem::path, std::shared_ptr<Engine::ParticleRenderer>> customParticles;
        std::map<std::filesystem::path, std::shared_ptr<Engine::PostEffectRenderer>> customPost;
    };
    struct PresentationPacket
    {
        std::shared_ptr<const RenderPacket> scene;
        std::shared_ptr<const PresentationResources> resources;
        std::shared_ptr<const UiDrawResources> ui;
    };
    class RenderPresentation final
    {
    public:
        /// <summary>表示世代だけから空・メッシュ・粒子・ポストエフェクト・UIを描画します。</summary>
        static bool Draw(const PresentationPacket& frame, ID3D12GraphicsCommandList* commands, unsigned int width,
                         unsigned int height, const SceneDrawOptions& options, Engine::OcclusionRenderer& occlusion,
                         MeshTelemetry& telemetry);
        /// <summary>不変のエミッタと表示姿勢から、指定した粒子の位置と透明度を取得します。</summary>
        static std::array<float, 4> Particle(const RenderNode<ParticleEmitterComponent>& emitter, unsigned int index,
                                             double seconds);
    };
}
