#pragma once
#include <SceneRuntime/Scene/SceneEnvironment.h>
#include <Engine/Input/InputActions.h>

namespace SceneRuntime
{
    /// <summary>共通再生へ必要なゲーム機能だけを注入します。未指定時は通常のシーン再生だけを行います。</summary>
    class PlaybackFeature
    {
    public:
        /// <summary>機能が所有する実行状態を解放します。</summary>
        virtual ~PlaybackFeature() = default;
        /// <summary>シーン切替後の機能状態を準備します。</summary>
        virtual void Initialize(SceneEnvironment&, const std::filesystem::path&, bool) {}
        /// <summary>ゲーム入力を占有する状態かを返します。</summary>
        virtual bool Active() const
        {
            return false;
        }
        /// <summary>背景の実行値を進めるかを返します。</summary>
        virtual bool BackgroundMotion() const
        {
            return true;
        }
        /// <summary>ポインター操作を占有する状態かを返します。</summary>
        virtual bool BlocksPointer() const
        {
            return false;
        }
        /// <summary>入力から機能の指示を準備します。</summary>
        virtual std::optional<UiEvent> Update(const Engine::InputActions&, bool, double, UiState&,
                                              const Engine::InputSnapshot*)
        {
            return {};
        }
        /// <summary>描画に先立つ機能の値を反映します。</summary>
        virtual void ApplyAnimation(SceneEnvironment&) {}
        /// <summary>機能が予約する演出を準備します。</summary>
        virtual void PreparePresentation(SceneEnvironment&, double, bool) {}
        /// <summary>成功したフレームの演出を適用します。</summary>
        virtual void ApplyPresentation(SceneEnvironment&) {}
        /// <summary>機能固有の音声がない場合はシーン音声を更新します。</summary>
        virtual void UpdateAudio(SceneEnvironment& environment, double, bool active)
        {
            environment.UpdateAudio(active);
        }
        /// <summary>未処理の指示を一度だけ取得します。</summary>
        virtual std::optional<UiEvent> TakeCommand()
        {
            return {};
        }
        /// <summary>入力の所有を一時停止します。</summary>
        virtual void Suspend() {}
        /// <summary>ポインター選択を機能へ渡します。</summary>
        virtual void SelectHovered(const SceneLayout&, const std::string&, UiState&) {}
        /// <summary>機能が処理したUI指示かを返します。</summary>
        virtual bool Activate(const UiEvent&, UiState&)
        {
            return false;
        }
    };
    using PlaybackFeatureFactory = std::function<std::unique_ptr<PlaybackFeature>()>;
}
