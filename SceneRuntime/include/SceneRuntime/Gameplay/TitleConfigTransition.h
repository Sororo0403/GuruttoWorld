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
    struct TitleMenuInput;
    class TitleConfigTransition final
    {
    public:
        /// <summary>設定を反映します。</summary>
        void Configure(const SceneLayout& layout, const MenuConfigTransition& configuration, UiState& state,
                       bool settingsScreen);
        bool Blocking() const
        {
            return phase_ == Phase::Entering || phase_ == Phase::Returning || (phase_ == Phase::Held && !inputReady_);
        }
        /// <summary>遷移中の入力を抑制します。</summary>
        bool BlockInput(const TitleMenuInput& input, bool pointerDown = false);
        /// <summary>設定画面の開閉に応じて遷移状態を更新します。</summary>
        void Observe(bool settingsOpen, UiState& state);
        /// <summary>画面への進入通知を取得して消去します。</summary>
        bool TakeEntered();
        /// <summary>画面からの帰還通知を取得して消去します。</summary>
        bool TakeReturned();
        bool Enabled() const
        {
            return configuration_.enabled;
        }
        bool Returning() const
        {
            return phase_ == Phase::Returning;
        }
        /// <summary>使用する資源と状態を準備します。</summary>
        void Prepare(SceneEnvironment& environment, double seconds, bool active);
        /// <summary>変更内容を反映します。</summary>
        void Apply(SceneEnvironment& environment);

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
        /// <summary>現在の状態を同期します。</summary>
        void Sync(UiState& state) const;
        /// <summary>操作の失敗を記録します。</summary>
        void Fail(SceneEnvironment& environment, const char* error);
        MenuConfigTransition configuration_;
        Phase phase_ = Phase::Idle;
        float elapsed_ = 0, clipDuration_ = 0;
        bool inputReady_ = true, settingsOpen_ = false, capture_ = false, entered_ = false, returned_ = false;
        std::string cameraId_;
        ScenePlacement from_;
        std::optional<ScenePlacement> lastPose_;
    };
}
