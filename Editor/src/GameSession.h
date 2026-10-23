#pragma once
#include "PlayState.h"
#include "TitlePreview.h"
#include <SceneRuntime/GamePlayback.h>
#include <Engine/Core/Log.h>
#include <SceneRuntime/SceneEnvironment.h>
#include <memory>
#include <stdexcept>

namespace Editor
{
    class GameSession final
    {
    public:
        enum class Command { Play, Pause, Stop, Step };
        const PlayState& State() const { return state_; }
        SceneRuntime::SceneEnvironment* Runtime() const { return runtime_.get(); }
        TitlePreview& Title() { return playback_.Title(); }
        SceneRuntime::GamePlayback& Playback() { return playback_; }
        std::optional<SceneRuntime::UiEvent> PrepareInput(const Engine::InputActions& actions,bool active,double seconds,const Engine::InputSnapshot* snapshot=nullptr) {return runtime_ ? playback_.PrepareInput(*runtime_,actions,active,seconds,snapshot) : std::nullopt;}
        /// <summary>編集中の配置から再生環境を作成し、一時停止中は再開します。GPU の完了後、Render の外で呼び出してください。</summary>
        bool Play(const Engine::DirectX12Renderer& renderer, const std::filesystem::path& root,
                  const SceneRuntime::SceneLayout& layout, std::string& error,
                  const std::filesystem::path& settingsPath = {},
                  std::shared_ptr<const Engine::ContentContext> context = {})
        {
            if (!state_.CanPlay()) { error="ゲームは既に再生中です"; return false; }
            if (state_.IsEditing())
            {
                try
                {
                    auto candidate=std::make_unique<SceneRuntime::SceneEnvironment>();
                    if (!context) context = Engine::ContentContext::Capture(root);
                    context->RequireRoot(root);
                    if (!candidate->Initialize(renderer, context, layout, error))
                        return false;
                    SceneRuntime::GamePlayback candidatePlayback;
                    candidatePlayback.Initialize(*candidate, root, settingsPath);
                    if (!candidate->StartScripts(error))
                        return false;
                    auto candidateSettings = candidatePlayback.SettingsPath();
                    auto candidateState = state_;
                    if (!candidateState.Play())
                        return false;
                    runtime_ = std::move(candidate);
                    playback_ = std::move(candidatePlayback);
                    settingsPath_ = std::move(candidateSettings);
                    state_ = candidateState;
                    command_.reset();
                    error.clear();
                    return true;
                }
                catch (const std::exception& exception) { error=exception.what(); return false; }
            }
            try
            {
                if (runtime_)
                    runtime_->Content()->RequireRoot(root);
            }
            catch (const std::exception& failure)
            {
                error = failure.what();
                return false;
            }
            error.clear();
            if(runtime_) runtime_->PauseAudio(false);
            return state_.Play();
        }
        /// <summary>取得済みの配置から再生環境を交換します。GPU の完了後、Render の外で呼び出してください。</summary>
        bool LoadScene(const Engine::DirectX12Renderer& renderer,const std::filesystem::path& root,const SceneRuntime::SceneLayout& layout,std::string& error)
        {
            if(state_.IsEditing()) {error="シーン切り替えには再生が必要です"; return false;}
            try
            {
                auto candidate = std::make_unique<SceneRuntime::SceneEnvironment>();
                if (runtime_->Content())
                    candidate->World().SetContentContext(runtime_->Content());
                if (!candidate->Initialize(renderer, root, layout, error))
                    return false;
                SceneRuntime::GamePlayback candidatePlayback;
                candidatePlayback.Initialize(*candidate, root, settingsPath_);
                if (!candidate->StartScripts(error))
                    return false;
                if (state_.CanStep())
                    candidate->PauseAudio(true);
                runtime_ = std::move(candidate);
                playback_ = std::move(candidatePlayback);
                command_.reset();
                error.clear();
                return true;
            }
            catch (const std::exception& failure)
            {
                error = failure.what();
                return false;
            }
        }
        /// <summary>保存されたシーンを現在の実行環境へ読み込みます。GPU の完了後、Render の外で呼び出してください。</summary>
        bool LoadScene(const std::filesystem::path& root,const std::filesystem::path& scene,std::string& error)
        {
            if(!runtime_ || state_.IsEditing()) {error="シーン切り替えには再生が必要です";return false;}
            try
            {
                runtime_->Content()->RequireRoot(root);
                if (!runtime_->LoadScene(scene, false, error))
                    return false;
                command_.reset();
                return true;
            }
            catch (const std::exception& failure)
            {
                error = failure.what();
                return false;
            }
        }
        bool Pause() { const bool result=state_.Pause(); if(result && runtime_) {runtime_->PauseAudio(true); runtime_->DiscardPendingInput(); runtime_->Ui().pressed.clear(); runtime_->Ui().hovered.clear();} return result; }
        /// <summary>再生環境を破棄して編集へ戻ります。GPU の完了後、Render の外で呼び出してください。</summary>
        bool Stop()
        {
            if (!state_.CanStop()) return false;
            runtime_.reset();command_.reset();
            return state_.Stop();
        }
        bool Update(double seconds, bool active)
        {
            auto candidateState = state_;
            if (!active || !runtime_ || !candidateState.Advance(seconds)) {
                if(runtime_) playback_.Suspend(*runtime_);
                return false;
            }
            command_=playback_.Update(*runtime_,seconds,true);
            if (!playback_.LastUpdateSucceeded()) return false;
            state_ = candidateState;
            return true;
        }
        std::optional<SceneRuntime::UiEvent> TakeCommand() {auto result=std::move(command_);command_.reset();return result;}
        bool Step()
        {
            if(!runtime_) return false;
            const auto seconds=runtime_->World().PhysicsStepSeconds();
            auto candidateState = state_;
            if (!candidateState.Step(seconds)) return false;
            Engine::InputActions neutralActions;Engine::InputSnapshot neutralSnapshot;neutralSnapshot.active=true;
            neutralActions.Update(neutralSnapshot);
            playback_.PrepareInput(*runtime_,neutralActions,true,seconds,&neutralSnapshot);
            command_=playback_.Update(*runtime_,seconds,true);
            runtime_->PauseAudio(true);
            if (!playback_.LastUpdateSucceeded())
                return false;
            state_ = candidateState;
            return true;
        }
        void Draw(ID3D12GraphicsCommandList* commands, unsigned int width, unsigned int height)
        {
            if (runtime_) runtime_->Draw(commands,width,height);
        }
    private:
        std::filesystem::path settingsPath_;
        std::optional<SceneRuntime::UiEvent> command_;
        PlayState state_;
        SceneRuntime::GamePlayback playback_;
        std::unique_ptr<SceneRuntime::SceneEnvironment> runtime_;
    };
}
