#pragma once
#include <SceneRuntime/Gameplay/TitleBindings.h>
#include <SceneRuntime/Gameplay/TitleUi.h>
#include <Engine/Input/InputActions.h>
#include <SceneRuntime/Scene/SceneEnvironment.h>
#include <optional>
#include <SceneRuntime/Gameplay/TitleConfigTransition.h>

namespace SceneRuntime
{
    class TitleController final
    {
    public:
        /// <summary>初期化します。</summary>
        void Initialize(const SceneRuntime::SceneLayout& layout, SceneRuntime::UiState& state,
                        const std::filesystem::path& settingsPath = {}, bool playIntro = false);
        /// <summary>画面演出に必要な状態を準備します。</summary>
        void PreparePresentation(SceneEnvironment& environment, double seconds, bool active);

    private:
        /// <summary>画面からの出発演出を反映します。</summary>
        void ApplyDeparture(SceneEnvironment& environment);

    public:
        /// <summary>画面演出の状態を反映します。</summary>
        void ApplyPresentation(SceneEnvironment& environment);
        /// <summary>画面演出の時間を進めます。</summary>
        void AdvancePresentation(SceneEnvironment& environment, double seconds, bool active);
        /// <summary>保留中のコマンドを取得して消去します。</summary>
        std::optional<UiEvent> TakeCommand();
        bool BlocksPointer() const
        {
            return configTransition_.Blocking() || (menu_ && menu_->IsTransitioning());
        }
        /// <summary>ポインターの下の対象を選択します。</summary>
        void SelectHovered(const SceneLayout& layout, const std::string& id, UiState& state);
        bool Active() const
        {
            return menu_.has_value();
        }
        void Suspend()
        {
            if (menu_)
                menu_->Update({});
        }
        bool BackgroundMotion() const
        {
            return !menu_ || menu_->GetSettings().backgroundMotion;
        }
        /// <summary>アニメーションを現在の表示へ反映します。</summary>
        void ApplyAnimation(SceneRuntime::SceneEnvironment& environment);
        /// <summary>再生状態に応じて音声を更新します。</summary>
        void UpdateAudio(SceneRuntime::SceneEnvironment& environment, double seconds, bool active);
        /// <summary>対象を有効にします。</summary>
        bool Activate(const SceneRuntime::UiEvent& event, SceneRuntime::UiState& state);
        /// <summary>状態を更新します。</summary>
        std::optional<SceneRuntime::UiEvent> Update(const Engine::InputActions& actions, bool active, double seconds,
                                                    SceneRuntime::UiState& state,
                                                    const Engine::InputSnapshot* snapshot = nullptr);

    private:
        bool Departing() const
        {
            return menu_ && menu_->IsTransitioning() && menu_->SelectedEntry() &&
                   !menu_->SelectedEntry()->transitionCameraTarget.empty();
        }
        /// <summary>出発演出とシーンの状態を同期します。</summary>
        void SyncDeparture(UiState& state) const;
        /// <summary>シーンの切り替えを予約します。</summary>
        void QueueScene(const std::string& scene);
        /// <summary>操作を適用します。</summary>
        void Action(SceneRuntime::TitleMenuAction action);
        /// <summary>開始要求と演出の状態を同期します。</summary>
        void SyncStartRequest(SceneRuntime::UiState& state);

        /// <summary>現在の状態を同期します。</summary>
        void Sync(SceneRuntime::UiState& state);
        SceneRuntime::TitleConfigTransition configTransition_;
        std::optional<SceneRuntime::TitleMenu> menu_;
        std::optional<SceneRuntime::UiEvent> pending_;
        std::optional<ScenePlacement> departureFrom_;
        std::filesystem::path settingsPath_;
        bool seekIntro_ = false;
        float fade_ = 0;
        size_t pendingCue_ = 0;
    };
}
