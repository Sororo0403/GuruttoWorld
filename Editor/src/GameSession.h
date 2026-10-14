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
        // Create and release resources outside Render after GPU idle.
        bool Play(const Engine::DirectX12Renderer& renderer, const std::filesystem::path& root,
            const SceneRuntime::SceneLayout& layout, std::string& error,const std::filesystem::path& settingsPath=SceneRuntime::GameSettings::UserPath())
        {
            if (!state_.CanPlay()) { error="ゲームは既に再生中です"; return false; }
            if (state_.IsEditing())
            {
                try
                {
                    auto candidate=std::make_unique<SceneRuntime::SceneEnvironment>();
                    if (!candidate->Initialize(renderer,root,layout,error)) return false;
                    runtime_=std::move(candidate);
                    settingsPath_=settingsPath;
                    playback_.Initialize(*runtime_,root,settingsPath_);
                }
                catch (const std::exception& exception) { error=exception.what(); return false; }
            }
            error.clear();
            if(runtime_) runtime_->PauseAudio(false);
            return state_.Play();
        }
        bool LoadScene(const Engine::DirectX12Renderer& renderer,const std::filesystem::path& root,const SceneRuntime::SceneLayout& layout,std::string& error)
        {
            if(state_.IsEditing()) {error="シーン切り替えには再生が必要です"; return false;}
            auto candidate=std::make_unique<SceneRuntime::SceneEnvironment>();
            if(!candidate->Initialize(renderer,root,layout,error)) return false;
            runtime_=std::move(candidate); playback_.Initialize(*runtime_,root,settingsPath_); state_.Stop(); return state_.Play();
        }
        bool LoadScene(const std::filesystem::path& root,const std::filesystem::path& scene,std::string& error)
        {
            if(!runtime_ || state_.IsEditing()) {error="シーン切り替えには再生が必要です";return false;}
            if(!runtime_->LoadScene(scene,false,error)) return false;
            playback_.Initialize(*runtime_,root,settingsPath_);
            return true;
        }
        bool Pause() { const bool result=state_.Pause(); if(result && runtime_) {runtime_->PauseAudio(true); runtime_->DiscardPendingInput(); runtime_->Ui().pressed.clear(); runtime_->Ui().hovered.clear();} return result; }
        bool Stop()
        {
            if (!state_.CanStop()) return false;
            runtime_.reset();command_.reset();
            return state_.Stop();
        }
        bool Update(double seconds, bool active)
        {
            if (!active || !runtime_ || !state_.Advance(seconds)) {
                if(runtime_) playback_.Suspend(*runtime_);
                return false;
            }
            command_=playback_.Update(*runtime_,seconds,true);
            return true;
        }
        std::optional<SceneRuntime::UiEvent> TakeCommand() {auto result=std::move(command_);command_.reset();return result;}
        bool Step()
        {
            if(!runtime_) return false;
            const auto seconds=runtime_->World().PhysicsStepSeconds();
            if (!state_.Step(seconds)) return false;
            runtime_->SetInputActions({},{});
            runtime_->MovePlayers(seconds,0,0);
            command_=playback_.Update(*runtime_,seconds,true);
            return true;
        }
        void Draw(ID3D12GraphicsCommandList* commands, unsigned int width, unsigned int height)
        {
            if (runtime_) runtime_->Draw(commands,width,height);
        }
    private:
        std::filesystem::path settingsPath_=SceneRuntime::GameSettings::UserPath();
        std::optional<SceneRuntime::UiEvent> command_;
        PlayState state_;
        SceneRuntime::GamePlayback playback_;
        std::unique_ptr<SceneRuntime::SceneEnvironment> runtime_;
    };
}
