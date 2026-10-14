#pragma once
#include <SceneRuntime/SceneEnvironment.h>
#include <SceneRuntime/SceneView.h>
#include <SceneRuntime/TitleMenu.h>
#include <algorithm>
#include <cmath>

namespace SceneRuntime
{
    // Shared presentation for the player and the editor's title preview.
    class TitleConfigTransition final
    {
    public:
        bool Blocking() const { return seconds_>=0 && (seconds_<Duration || !inputReady_); }
        bool BlockInput(const TitleMenuInput& input,bool pointerDown=false)
        {
            if(seconds_>=Duration && input.active && !pointerDown && input.keyboardButtons==0 &&
                input.gamepadButtons==0 && std::abs(input.stickX)<.3f && std::abs(input.stickY)<.3f)
                inputReady_=true;
            return Blocking();
        }
        void Observe(bool settingsOpen,UiState& state)
        {
            if(state.Value("configTransitionEnabled")!=1) return;
            if(settingsOpen && !settingsOpen_) {seconds_=0; inputReady_=false; captureCamera_=true;}
            if(!settingsOpen && settingsOpen_) {restore_=seconds_>=0; seconds_=-1; captureCamera_=false;}
            settingsOpen_=settingsOpen;
            Sync(state);
        }
        void Advance(SceneEnvironment& environment,double seconds,bool active)
        {
            if(environment.Ui().Value("configTransitionEnabled")!=1 || (seconds_<0 && !restore_)) return;
            if(captureCamera_) {
                if(const auto* camera=SceneView::CameraObject(environment.World().Layout())) {
                    cameraId_=camera->id; position_=camera->position; rotation_=camera->rotation; scale_=camera->scale;
                }
                captureCamera_=false;
            }
            if(seconds_>=0 && active && std::isfinite(seconds))
                seconds_=std::min(Duration,seconds_+static_cast<float>(std::clamp(seconds,0.0,0.1)));
            Sync(environment.Ui());
            environment.World().Animate({{"configEnterTime",std::max(0.0f,seconds_)}});
            restore_=false;
            if(seconds_<0 || cameraId_.empty()) return;
            const float t=std::clamp((seconds_-.45f)/1.55f,0.0f,1.0f),blend=t*t*(3-2*t);
            auto position=position_,rotation=rotation_;
            const std::array<float,3> targetPosition{9.0f,5.6f,10.3f},targetRotation{0,0,0};
            for(size_t axis=0;axis<3;++axis) {
                position[axis]+=(targetPosition[axis]-position[axis])*blend;
                rotation[axis]+=(targetRotation[axis]-rotation[axis])*blend;
            }
            environment.World().SetLocalTransform(cameraId_,position,rotation,scale_);
        }
    private:
        void Sync(UiState& state) const
        {
            if(seconds_>=0) state.values["screen"]=seconds_<Duration ? 3.0f : 1.0f;
            state.values["configEnterTime"]=std::max(0.0f,seconds_);
        }
        static constexpr float Duration=2.0f;
        float seconds_=-1;
        bool inputReady_=true,settingsOpen_=false,captureCamera_=false,restore_=false;
        std::string cameraId_;
        std::array<float,3> position_{},rotation_{},scale_{1,1,1};
    };
}
