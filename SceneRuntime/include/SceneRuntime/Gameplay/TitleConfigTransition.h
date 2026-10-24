#pragma once
#include <SceneRuntime/Scene/SceneEnvironment.h>
#include <SceneRuntime/Rendering/SceneView.h>
#include <SceneRuntime/Scene/SceneTransforms.h>
#include <Engine/Core/Log.h>
#include <algorithm>
#include <cmath>
#include <utility>

namespace SceneRuntime
{
    class TitleConfigTransition final
    {
    public:
        void Configure(const SceneLayout& layout, const MenuConfigTransition& configuration, UiState& state,
                       bool settingsScreen)
        {
            *this = {};
            configuration_ = configuration;
            configuration_.enabled = configuration_.enabled && state.Value(configuration_.enabledState, 1) != 0;
            for (const auto& object : layout.objects)
                if (object.animation)
                    for (const auto& track : object.animation->tracks)
                        if (!configuration_.clock.empty() && track.clock == configuration_.clock && !track.keys.empty())
                            clipDuration_ = std::max(clipDuration_, track.keys.back().time);
            clipDuration_ = std::max(.01f, clipDuration_);
            if (configuration_.enabled && settingsScreen)
            {
                phase_ = Phase::Held;
                settingsOpen_ = true;
                inputReady_ = true;
            }
            Sync(state);
        }
        bool Blocking() const
        {
            return phase_ == Phase::Entering || phase_ == Phase::Returning || (phase_ == Phase::Held && !inputReady_);
        }
        bool BlockInput(const TitleMenuInput& input, bool pointerDown = false)
        {
            if (phase_ == Phase::Held && input.active && !pointerDown && input.keyboardButtons == 0 &&
                input.gamepadButtons == 0 && std::abs(input.stickX) < .3f && std::abs(input.stickY) < .3f)
                inputReady_ = true;
            return Blocking();
        }
        void Observe(bool settingsOpen, UiState& state)
        {
            if (!configuration_.enabled)
            {
                settingsOpen_ = settingsOpen;
                return;
            }
            if (settingsOpen && !settingsOpen_)
            {
                phase_ = Phase::Entering;
                elapsed_ = 0;
                capture_ = true;
                inputReady_ = false;
            }
            if (!settingsOpen && settingsOpen_)
            {
                phase_ = Phase::Returning;
                elapsed_ = 0;
                capture_ = true;
                inputReady_ = false;
            }
            settingsOpen_ = settingsOpen;
            Sync(state);
        }
        bool TakeEntered()
        {
            const bool entered = entered_;
            entered_ = false;
            return entered;
        }
        bool TakeReturned()
        {
            const bool returned = returned_;
            returned_ = false;
            return returned;
        }
        bool Enabled() const
        {
            return configuration_.enabled;
        }
        bool Returning() const
        {
            return phase_ == Phase::Returning;
        }
        void Prepare(SceneEnvironment& environment, double seconds, bool active)
        {
            if (!configuration_.enabled || !active || !std::isfinite(seconds) || seconds <= 0)
                return;
            const auto* camera = SceneView::CameraObject(environment.World().Layout());
            if (!camera)
            {
                Fail(environment, "Config transition camera is missing");
                return;
            }
            cameraId_ = camera->id;
            if (capture_)
            {
                if (phase_ == Phase::Returning && lastPose_)
                    from_ = *lastPose_;
                else if (!Pose(environment, cameraId_, from_))
                {
                    Fail(environment, "Config transition camera transform is invalid");
                    return;
                }
                capture_ = false;
            }
            const float duration =
                phase_ == Phase::Returning ? configuration_.returnDuration : configuration_.enterDuration;
            if (phase_ == Phase::Entering || phase_ == Phase::Returning)
                elapsed_ = std::min(duration, elapsed_ + static_cast<float>(std::clamp(seconds, 0.0, .1)));
            Sync(environment.Ui());
        }
        void Apply(SceneEnvironment& environment)
        {
            if (!configuration_.enabled || phase_ == Phase::Idle)
                return;
            ScenePlacement target;
            const auto& id =
                phase_ == Phase::Returning ? configuration_.returnCameraTarget : configuration_.cameraTarget;
            if (!Pose(environment, id.empty() ? cameraId_ : id, target))
            {
                Fail(environment, "Config transition target is missing or cannot be decomposed");
                return;
            }
            float blend = 1;
            if (phase_ == Phase::Entering)
                blend = std::clamp((elapsed_ - configuration_.zoomDelay) /
                                       std::max(.001f, configuration_.enterDuration - configuration_.zoomDelay),
                                   0.0f, 1.0f);
            if (phase_ == Phase::Returning)
                blend = std::clamp(elapsed_ / configuration_.returnDuration, 0.0f, 1.0f);
            blend = blend * blend * (3 - 2 * blend);
            auto pose = target;
            if (phase_ != Phase::Held)
                for (size_t axis = 0; axis < 3; ++axis)
                {
                    pose.position[axis] = from_.position[axis] + (target.position[axis] - from_.position[axis]) * blend;
                    // オイラー角の折り返しで一周してしまわないよう、最短の角度差を補間します。
                    pose.rotation[axis] =
                        from_.rotation[axis] +
                        std::remainder(target.rotation[axis] - from_.rotation[axis], 6.283185307f) * blend;
                }
            if (!environment.World().SetWorldTransform(cameraId_, pose.position, pose.rotation, pose.scale))
            {
                Fail(environment, "Config transition camera pose was rejected");
                return;
            }
            lastPose_ = pose;
            if (phase_ == Phase::Entering && elapsed_ >= configuration_.enterDuration)
            {
                phase_ = Phase::Held;
                entered_ = true;
            }
            else if (phase_ == Phase::Returning && elapsed_ >= configuration_.returnDuration)
            {
                phase_ = Phase::Idle;
                returned_ = true;
                inputReady_ = true;
            }
            Sync(environment.Ui());
        }

    private:
        enum class Phase
        {
            Idle,
            Entering,
            Held,
            Returning
        };
        static bool Pose(const SceneEnvironment& environment, const std::string& id, ScenePlacement& pose)
        {
            DirectX::XMFLOAT4X4 matrix;
            return environment.World().WorldMatrix(id, matrix) && SceneTransforms::ReadTransform(matrix, {}, pose);
        }
        void Sync(UiState& state) const
        {
            if (!configuration_.enabled)
                return;
            const auto& values = phase_ == Phase::Entering || phase_ == Phase::Returning
                                     ? configuration_.transitionValues
                                 : phase_ == Phase::Held || settingsOpen_ ? configuration_.heldValues
                                                                          : configuration_.idleValues;
            for (const auto& [key, value] : values)
                state.values[key] = value;
            const float progress = phase_ == Phase::Held        ? 1
                                   : phase_ == Phase::Entering  ? elapsed_ / configuration_.enterDuration
                                   : phase_ == Phase::Returning ? 1 - elapsed_ / configuration_.returnDuration
                                                                : 0;
            if (!configuration_.clock.empty())
                state.values[configuration_.clock] = std::clamp(progress, 0.0f, 1.0f) * clipDuration_;
        }
        void Fail(SceneEnvironment& environment, const char* error)
        {
            Engine::Log::Warning(error);
            configuration_.enabled = false;
            phase_ = Phase::Idle;
            const auto& values = settingsOpen_ ? configuration_.heldValues : configuration_.idleValues;
            for (const auto& [key, value] : values)
                environment.Ui().values[key] = value;
        }
        MenuConfigTransition configuration_;
        Phase phase_ = Phase::Idle;
        float elapsed_ = 0, clipDuration_ = 0;
        bool inputReady_ = true, settingsOpen_ = false, capture_ = false, entered_ = false, returned_ = false;
        std::string cameraId_;
        ScenePlacement from_;
        std::optional<ScenePlacement> lastPose_;
    };
}
