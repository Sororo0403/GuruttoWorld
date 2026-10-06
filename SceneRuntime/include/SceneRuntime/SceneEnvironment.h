#pragma once
#include <SceneRuntime/ScenePresentation.h>

namespace SceneRuntime
{
    class SceneEnvironment final
    {
    public:
        bool Initialize(Engine::DirectX12Renderer& renderer, const std::filesystem::path& root,
            const std::filesystem::path& scenePath, std::string& error);
        bool Initialize(Engine::DirectX12Renderer& renderer, const std::filesystem::path& root,
            SceneLayout layout, std::string& error);
        const SceneWorld& World() const { return world_; }
        double MotionSeconds() const { return seconds_; }
        bool MotionEnabled() const { return motionEnabled_; }
        std::array<float,3> CameraPosition() const;
        std::array<float,4> Particle(unsigned int index) const;
        void Update(double deltaSeconds, bool enabled, bool active);
        void Draw(ID3D12GraphicsCommandList* commands, unsigned int width, unsigned int height) const;
    private:
        SceneWorld world_;
        ScenePresentation presentation_;
        double seconds_=0;
        bool motionEnabled_=true;
    };
}
