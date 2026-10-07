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
        void AudioCue(const std::string& cue) { audio_.Cue(cue); }
        void PauseAudio(bool paused) { audio_.Pause(paused); }
        UiEvent Click(const std::string& object);
        void Update(double deltaSeconds, bool enabled, bool active);
        void SetInputActions(std::map<std::string,float> values,std::map<std::string,bool> pressed) { world_.SetInputActions(std::move(values),std::move(pressed)); }
        bool MovePlayers(double seconds, float horizontal, float vertical, bool jump=false) { return world_.MovePlayers(seconds,horizontal,vertical,jump); }
        /// <summary>時計を指定して演出をプレビューします。未開始の開始演出には負値を指定します。</summary>
        void SeekAnimation(float sceneSeconds, float motionSeconds, float startSeconds = -1);
        void Draw(ID3D12GraphicsCommandList* commands, unsigned int width, unsigned int height) const;
    private:
        /// <summary>保存した移動クリップを現在位置から評価し、メニュー選択へ追従します。</summary>
        void AnimateCameraFocus(float elapsed);
        std::array<float,3> focusPosition_{},focusRotation_{};
        float focusSeconds_=0;
        int focusRequested_=0;
        bool focusEngaged_=false;
        UiState uiState_;
        SceneAudio audio_;
        SceneWorld world_;
        ScenePresentation presentation_;
        double seconds_=0;
        bool motionEnabled_=true;
        float sceneSeconds_=0, startSeconds_=-1;
    };
}
