#pragma once
#include "PlayState.h"
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
        // Create and release resources outside Render after GPU idle.
        bool Play(const Engine::DirectX12Renderer& renderer, const std::filesystem::path& root,
            const SceneRuntime::SceneLayout& layout, std::string& error)
        {
            if (!state_.CanPlay()) { error="ゲームは既に再生中です"; return false; }
            if (state_.IsEditing())
            {
                try
                {
                    auto candidate=std::make_unique<SceneRuntime::SceneEnvironment>();
                    if (!candidate->Initialize(renderer,root,layout,error)) return false;
                    std::string audioError;
                    if(!candidate->StartAudio(root,audioError)) Engine::Log::Warning(audioError);
                    runtime_=std::move(candidate);
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
            std::string audioError; if(!candidate->StartAudio(root,audioError)) Engine::Log::Warning(audioError);
            runtime_=std::move(candidate); state_.Stop(); return state_.Play();
        }
        bool Pause() { const bool result=state_.Pause(); if(result && runtime_) {runtime_->PauseAudio(true); runtime_->Ui().pressed.clear(); runtime_->Ui().hovered.clear();} return result; }
        bool Stop()
        {
            if (!state_.CanStop()) return false;
            runtime_.reset();
            return state_.Stop();
        }
        bool Update(double seconds, bool active)
        {
            if(runtime_) runtime_->UpdateAudio(active && state_.CanPause());
            if (!active || !runtime_ || !state_.Advance(seconds)) return false;
            runtime_->Update(seconds,true,true);
            return true;
        }
        bool Step()
        {
            if (!runtime_ || !state_.Step()) return false;
            runtime_->MovePlayers(PlayState::StepSeconds,0,0);
            runtime_->Update(PlayState::StepSeconds,true,true);
            return true;
        }
        void Draw(ID3D12GraphicsCommandList* commands, unsigned int width, unsigned int height)
        {
            if (runtime_) runtime_->Draw(commands,width,height);
        }
    private:
        PlayState state_;
        std::unique_ptr<SceneRuntime::SceneEnvironment> runtime_;
    };
}
