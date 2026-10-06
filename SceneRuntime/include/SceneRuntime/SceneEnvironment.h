#pragma once
#include <SceneRuntime/ScenePresentation.h>
#include <SceneRuntime/SceneAudio.h>

namespace SceneRuntime
{
    class SceneEnvironment final
    {
    public:
        bool Initialize(const Engine::DirectX12Renderer& renderer, const std::filesystem::path& root,
            const std::filesystem::path& scenePath, std::string& error);
        bool Initialize(const Engine::DirectX12Renderer& renderer, const std::filesystem::path& root,
            SceneLayout layout, std::string& error);
        const SceneWorld& World() const { return world_; }
        double MotionSeconds() const { return seconds_; }
        bool MotionEnabled() const { return motionEnabled_; }
        std::array<float,3> CameraPosition() const;
        std::array<float,4> Particle(unsigned int index) const;
        UiState& Ui() { return uiState_; }
        bool StartAudio(const std::filesystem::path& root,std::string& error) { return audio_.Initialize(root,world_.Layout(),error); }
        void UpdateAudio(bool active) { audio_.Update(world_.Layout(),uiState_,active); }
        void PauseAudio(bool paused) { audio_.Pause(paused); }
        UiEvent Click(const std::string& object);
        void Update(double deltaSeconds, bool enabled, bool active);
        void Draw(ID3D12GraphicsCommandList* commands, unsigned int width, unsigned int height) const;
    private:
        UiState uiState_;
        SceneAudio audio_;
        SceneWorld world_;
        ScenePresentation presentation_;
        double seconds_=0;
        bool motionEnabled_=true;
    };
}
