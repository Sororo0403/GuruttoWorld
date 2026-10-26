#pragma once
#include <SceneRuntime/Gameplay/TitleController.h>
#include <Engine/Core/Log.h>

namespace SceneRuntime
{
    // プラットフォームのアダプターが入力と描画先を提供し、ゲームのルールはここで管理します。
    class GamePlayback final
    {
    public:
        /// <summary>準備時に解決した設定保存先を取得します。</summary>
        const std::filesystem::path& SettingsPath() const
        {
            return settingsPath_;
        }
        TitleController& Title()
        {
            return title_;
        }
        /// <summary>初期化します。</summary>
        void Initialize(SceneEnvironment& environment, const std::filesystem::path& root,
                        const std::filesystem::path& settingsPath = {}, bool playIntro = true);
        /// <summary>入力と移動予約を準備し、物理更新は成功判定を共有するUpdateまで遅らせます。</summary>
        std::optional<UiEvent> PrepareInput(SceneEnvironment& environment, const Engine::InputActions& actions,
                                            bool acceptsInput, double seconds,
                                            const Engine::InputSnapshot* snapshot = nullptr);
        /// <summary>最後のワールド更新と副作用予約の確定を返します。シーン操作の結果は環境から別に取得します。</summary>
        bool LastUpdateSucceeded() const
        {
            return lastUpdateSucceeded_;
        }
        /// <summary>状態を更新します。</summary>
        std::optional<UiEvent> Update(SceneEnvironment& environment, double seconds, bool active);
        /// <summary>実行中の処理を一時停止します。</summary>
        void Suspend(SceneEnvironment& environment);
        /// <summary>ポインターの状態を取得します。</summary>
        UiEvent Pointer(SceneEnvironment& environment, unsigned int width, unsigned int height, UiPointerInput input,
                        bool active);
        /// <summary>ショートカット入力を処理します。</summary>
        UiEvent Shortcut(SceneEnvironment& environment, const Engine::InputActions& actions, unsigned int width,
                         unsigned int height, bool active);

    private:
        /// <summary>現在の素材から依存情報を更新します。</summary>
        void Refresh(SceneEnvironment& environment);
        bool lastUpdateSucceeded_ = false;
        std::filesystem::path root_, settingsPath_;
        uint64_t revision_ = 0;
        bool playIntro_ = true;
        std::optional<UiEvent> pending_;
        TitleController title_;
        bool pointerReady_ = false, pointerDown_ = false;
    };
}
