#pragma once
#include "PlayState.h"
#include <SceneRuntime/TitleEnvironment.h>
#include <memory>
#include <stdexcept>

namespace Editor
{
    class GameSession final
    {
    public:
        enum class Command { Play, Pause, Stop, Step };
        const PlayState& State() const { return state_; }
        const SceneRuntime::TitleEnvironment* Runtime() const { return runtime_.get(); }
        // Create and release resources outside Render after GPU idle.
        bool Play(Engine::DirectX12Renderer& renderer, const std::filesystem::path& root,
            const SceneRuntime::SceneLayout& layout, std::string& error)
        {
            if (!state_.CanPlay()) { error="Game is already playing"; return false; }
            if (state_.IsEditing())
            {
                try
                {
                    auto candidate=std::make_unique<SceneRuntime::TitleEnvironment>();
                    if (!candidate->Initialize(renderer,root,layout,error)) return false;
                    runtime_=std::move(candidate);
                }
                catch (const std::exception& exception) { error=exception.what(); return false; }
            }
            error.clear();
            return state_.Play();
        }
        bool Pause() { return state_.Pause(); }
        bool Stop()
        {
            if (!state_.CanStop()) return false;
            runtime_.reset();
            return state_.Stop();
        }
        bool Update(double seconds, bool active)
        {
            if (!active || !runtime_ || !state_.Advance(seconds)) return false;
            runtime_->Update(seconds,true,true);
            return true;
        }
        bool Step()
        {
            if (!runtime_ || !state_.Step()) return false;
            runtime_->Update(PlayState::StepSeconds,true,true);
            return true;
        }
        void Draw(ID3D12GraphicsCommandList* commands, unsigned int width, unsigned int height)
        {
            if (runtime_) runtime_->Draw(commands,width,height);
        }
    private:
        PlayState state_;
        std::unique_ptr<SceneRuntime::TitleEnvironment> runtime_;
    };
}
