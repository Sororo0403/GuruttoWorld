#pragma once
#include <SceneRuntime/Scene/EntityHandle.h>
#include <SceneRuntime/Ui/UiPacket.h>
#include <SceneRuntime/Rendering/EnvironmentComponents.h>
#include <DirectXMath.h>
#include <Engine/Graphics/Materials/PostEffectSettings.h>
#include <filesystem>
#include <Engine/Animation/SkinMatrix.h>
#include <optional>
#include <string>
#include <vector>

namespace SceneRuntime
{
    struct RenderBounds
    {
        std::array<float, 3> center{}, extent{};
    };
    struct RenderSkin
    {
        std::vector<std::vector<Engine::SkinMatrix>> palettes;
        RenderBounds bounds;
    };
    /// <summary>GPU実装をCPU側へ公開せず、packetが使う資源世代の寿命を保持します。</summary>
    class RenderResource
    {
    public:
        /// <summary>保持していた資源の参照を解放します。</summary>
        virtual ~RenderResource() = default;
    };
    struct RenderBinding
    {
        std::uint64_t device = 0, generation = 0;
        std::shared_ptr<const RenderResource> resource;
        RenderBounds bounds;
        std::shared_ptr<const RenderSkin> skin;
    };
    class RenderResourceResolver
    {
    public:
        /// <summary>表示資源の解決先を解放します。</summary>
        virtual ~RenderResourceResolver() = default;
        /// <summary>現在の候補の表示資源を解決し、世代と所有参照を返します。</summary>
        virtual std::optional<RenderBinding> Resolve(EntityHandle entity, std::string& error) = 0;
        /// <summary>影など、packet全体が共有する表示資源を保持します。</summary>
        virtual std::shared_ptr<const RenderResource> Scene() const
        {
            return {};
        }
    };
    struct RenderObject
    {
        EntityHandle entity;
        std::string id, visibleWhen;
        DirectX::XMFLOAT4X4 matrix{}, rotation{};
        RenderBinding binding;
        std::vector<float> lodDistances;
        bool instancing = false, occlusion = false;
    };
    template <class T> struct RenderNode
    {
        EntityHandle entity;
        std::string id;
        DirectX::XMFLOAT4X4 matrix{}, rotation{};
        T settings;
    };
    struct RenderViewSettings
    {
        std::string mainCamera;
        FogSettings fog;
        Engine::PostEffectSettings postEffects;
        std::array<float, 4> background{};
        std::filesystem::path skyShader, oceanShader, particleShader, postShader;
    };
    struct RenderLocalLight
    {
        bool spot = false;
        std::size_t index = 0;
    };
    struct RenderPacket
    {
        std::uint64_t frame = 0, device = 0;
        RenderViewSettings settings;
        std::shared_ptr<const UiPacket> ui;
        std::shared_ptr<const RenderResource> sceneResources;
        std::vector<RenderObject> objects;
        std::vector<RenderNode<CameraComponent>> cameras;
        std::vector<RenderNode<CameraSwayComponent>> cameraSways;
        std::vector<RenderNode<SkyComponent>> skies;
        std::vector<RenderNode<ParticleEmitterComponent>> particles;
        std::vector<RenderNode<DirectionalLightComponent>> directionalLights;
        std::vector<RenderNode<PointLightComponent>> pointLights;
        std::vector<RenderNode<SpotLightComponent>> spotLights;
        std::vector<RenderLocalLight> localLights;
    };
}
