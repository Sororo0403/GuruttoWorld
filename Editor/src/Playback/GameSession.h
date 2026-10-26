#pragma once
#include "PlayState.h"
#include "../Ui/TitlePreview.h"
#include <SceneRuntime/Gameplay/GamePlayback.h>
#include <Engine/Core/Log.h>
#include <SceneRuntime/Scene/SceneEnvironment.h>
#include <memory>
#include <stdexcept>

namespace Editor
{
    class GameSession final
    {
    public:
        enum class Command
        {
            Play,
            Pause,
            Stop,
            Step
        };
        const PlayState& State() const
        {
            return state_;
        }
        SceneRuntime::SceneEnvironment* Runtime() const
        {
            return runtime_.get();
        }
        SceneRuntime::GamePlayback& Playback()
        {
            return playback_;
        }
        std::optional<SceneRuntime::UiEvent> PrepareInput(const Engine::InputActions& actions, bool active,
                                                          double seconds,
                                                          const Engine::InputSnapshot* snapshot = nullptr)
        {
            return runtime_ ? playback_.PrepareInput(*runtime_, actions, active, seconds, snapshot) : std::nullopt;
        }
        /// <summary>編集中の配置から再生環境を作成し、一時停止中は再開します。GPU の完了後、Render の外で呼び出してください。</summary>
        bool Play(const Engine::DirectX12Renderer& renderer, const std::filesystem::path& root,
                  const SceneRuntime::SceneLayout& layout, std::string& error,
                  const std::filesystem::path& settingsPath = {},
                  std::shared_ptr<const Engine::ContentContext> context = {});
        /// <summary>取得済みの配置から再生環境を交換します。GPU の完了後、Render の外で呼び出してください。</summary>
        bool LoadScene(const Engine::DirectX12Renderer& renderer, const std::filesystem::path& root,
                       const SceneRuntime::SceneLayout& layout, std::string& error);
        /// <summary>保存されたシーンを現在の実行環境へ読み込みます。GPU の完了後、Render の外で呼び出してください。</summary>
        bool LoadScene(const std::filesystem::path& root, const std::filesystem::path& scene, std::string& error);
        /// <summary>再生を一時停止します。</summary>
        bool Pause();
        /// <summary>再生環境を破棄して編集へ戻ります。GPU の完了後、Render の外で呼び出してください。</summary>
        bool Stop();
        /// <summary>状態を更新します。</summary>
        bool Update(double seconds, bool active);
        /// <summary>保留中のコマンドを取得して消去します。</summary>
        std::optional<SceneRuntime::UiEvent> TakeCommand();
        /// <summary>一時停止中の状態を指定時間だけ進めます。</summary>
        bool Step();
        void Draw(ID3D12GraphicsCommandList* commands, unsigned int width, unsigned int height)
        {
            if (runtime_)
                runtime_->Draw(commands, width, height);
        }

    private:
        std::filesystem::path settingsPath_;
        std::optional<SceneRuntime::UiEvent> command_;
        PlayState state_;
        SceneRuntime::GamePlayback playback_;
        std::unique_ptr<SceneRuntime::SceneEnvironment> runtime_;
    };
}
