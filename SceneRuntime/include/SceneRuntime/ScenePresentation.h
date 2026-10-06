#pragma once
#include <SceneRuntime/SceneView.h>
#include <Engine/Graphics/Renderers/SpriteRenderer.h>
#include <Engine/Graphics/Renderers/ParticleRenderer.h>

namespace SceneRuntime
{
    class ScenePresentation final
    {
    public:
        bool Initialize(const Engine::DirectX12Renderer& renderer, const std::filesystem::path& root, std::string& error);
        void Draw(ID3D12GraphicsCommandList* commands, const SceneWorld& world, unsigned int width,
            unsigned int height, const Engine::Camera* sceneCamera=nullptr, double seconds=0, bool motionEnabled=true) const;
        static std::array<float,4> Particle(const SceneWorld& world, const ScenePlacement& placement,
            unsigned int index, double seconds);
    private:
        void DrawSky(ID3D12GraphicsCommandList* commands, const SceneLayout& layout,
            unsigned int width, unsigned int height, double seconds) const;
        void DrawParticles(ID3D12GraphicsCommandList* commands, const SceneWorld& world,
            const Engine::Camera& camera, double seconds) const;
        Engine::SpriteRenderer sky_;
        Engine::ParticleRenderer particles_;
    };
}
