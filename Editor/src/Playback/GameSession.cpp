#include "GameSession.h"

namespace Editor
{
    auto GameSession::Play(const Engine::DirectX12Renderer& renderer, const std::filesystem::path& root,
                           const SceneRuntime::SceneLayout& layout, std::string& error,
                           const std::filesystem::path& settingsPath,
                           std::shared_ptr<const Engine::ContentContext> context) -> bool
    {
        if (!state_.CanPlay())
        {
            error = "ゲームは既に再生中です";
            return false;
        }
        if (state_.IsEditing())
        {
            try
            {
                auto candidate = std::make_unique<SceneRuntime::SceneEnvironment>();
                if (!context)
                    context = Engine::ContentContext::Capture(root);
                context->RequireRoot(root);
                if (!candidate->Initialize(renderer, context, layout, error))
                    return false;
                SceneRuntime::GamePlayback candidatePlayback;
                candidatePlayback.Initialize(*candidate, root, settingsPath, true,
                                             std::make_unique<SceneRuntime::TitleController>());
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
            catch (const std::exception& exception)
            {
                error = exception.what();
                return false;
            }
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
        if (runtime_)
            runtime_->PauseAudio(false);
        return state_.Play();
    }

    auto GameSession::LoadScene(const Engine::DirectX12Renderer& renderer, const std::filesystem::path& root,
                                const SceneRuntime::SceneLayout& layout, std::string& error) -> bool
    {
        if (state_.IsEditing())
        {
            error = "シーン切り替えには再生が必要です";
            return false;
        }
        try
        {
            auto candidate = std::make_unique<SceneRuntime::SceneEnvironment>();
            if (runtime_->Content())
                candidate->World().SetContentContext(runtime_->Content());
            if (!candidate->Initialize(renderer, root, layout, error))
                return false;
            SceneRuntime::GamePlayback candidatePlayback;
            candidatePlayback.Initialize(*candidate, root, settingsPath_, true,
                                         std::make_unique<SceneRuntime::TitleController>());
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

    auto GameSession::LoadScene(const std::filesystem::path& root, const std::filesystem::path& scene,
                                std::string& error) -> bool
    {
        if (!runtime_ || state_.IsEditing())
        {
            error = "シーン切り替えには再生が必要です";
            return false;
        }
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

    auto GameSession::Pause() -> bool
    {
        const bool result = state_.Pause();
        if (result && runtime_)
        {
            runtime_->PauseAudio(true);
            runtime_->DiscardPendingInput();
            runtime_->Ui().pressed.clear();
            runtime_->Ui().hovered.clear();
        }
        return result;
    }

    auto GameSession::Stop() -> bool
    {
        if (!state_.CanStop())
            return false;
        runtime_.reset();
        command_.reset();
        return state_.Stop();
    }

    auto GameSession::Update(double seconds, bool active) -> bool
    {
        auto candidateState = state_;
        if (!active || !runtime_ || !candidateState.Advance(seconds))
        {
            if (runtime_)
                playback_.Suspend(*runtime_);
            return false;
        }
        command_ = playback_.Update(*runtime_, seconds, true);
        if (!playback_.LastUpdateSucceeded())
            return false;
        state_ = candidateState;
        return true;
    }

    auto GameSession::TakeCommand() -> std::optional<SceneRuntime::UiEvent>
    {
        auto result = std::move(command_);
        command_.reset();
        return result;
    }

    auto GameSession::Step() -> bool
    {
        if (!runtime_)
            return false;
        const auto seconds = runtime_->World().PhysicsStepSeconds();
        auto candidateState = state_;
        if (!candidateState.Step(seconds))
            return false;
        Engine::InputActions neutralActions;
        Engine::InputSnapshot neutralSnapshot;
        neutralSnapshot.active = true;
        neutralActions.Update(neutralSnapshot);
        playback_.PrepareInput(*runtime_, neutralActions, true, seconds, &neutralSnapshot);
        command_ = playback_.Update(*runtime_, seconds, true);
        runtime_->PauseAudio(true);
        if (!playback_.LastUpdateSucceeded())
            return false;
        state_ = candidateState;
        return true;
    }
}
