#include <SceneRuntime/Gameplay/GamePlayback.h>
#include <SceneRuntime/Project/GameSettings.h>

namespace SceneRuntime
{
    auto GamePlayback::Initialize(SceneEnvironment& environment, const std::filesystem::path& root,
                                  const std::filesystem::path& settingsPath, bool playIntro,
                                  std::unique_ptr<PlaybackFeature> feature) -> void
    {
        feature_ = feature ? std::move(feature) : std::make_unique<PlaybackFeature>();
        root_ = root;
        settingsPath_ = settingsPath.empty() ? GameSettings::UserPath(*environment.Content()) : settingsPath;
        playIntro_ = playIntro;
        pending_.reset();
        Refresh(environment);
        std::string error;
        if (!environment.StartAudio(root, error))
            Engine::Log::Warning(error);
        environment.Ui().values["volumeGain"] = GameSettings::Load(settingsPath_).Gain();
        feature_->ApplyAnimation(environment);
    }

    auto GamePlayback::PrepareInput(SceneEnvironment& environment, const Engine::InputActions& actions,
                                    bool acceptsInput, double seconds, const Engine::InputSnapshot* snapshot)
        -> std::optional<UiEvent>
    {
        if (revision_ != environment.SceneRevision())
            Refresh(environment);
        const bool input = acceptsInput && environment.Ui().focused.empty();
        auto command = feature_->Update(actions, input, seconds, environment.Ui(), snapshot);
        const bool gameplay = input && !feature_->Active();
        environment.SetInputActions(gameplay ? actions.Values() : std::map<std::string, float>{},
                                    gameplay ? actions.PressedValues() : std::map<std::string, bool>{});
        const auto& mapping = environment.World().Layout().settings.playerInputs;
        environment.PrepareMovement(gameplay ? actions.Value(mapping[0]) - actions.Value(mapping[1]) : 0,
                                    gameplay ? actions.Value(mapping[2]) - actions.Value(mapping[3]) : 0,
                                    gameplay && actions.Pressed(mapping[4]));
        if (command)
            pending_ = command;
        return std::nullopt;
    }

    auto GamePlayback::Update(SceneEnvironment& environment, double seconds, bool active) -> std::optional<UiEvent>
    {
        lastUpdateSucceeded_ = false;
        if (!active)
        {
            Suspend(environment);
            return std::nullopt;
        }
        if (revision_ != environment.SceneRevision())
            Refresh(environment);
        feature_->ApplyAnimation(environment);
        feature_->UpdateAudio(environment, seconds, active);
        feature_->PreparePresentation(environment, seconds, active);
        if (!environment.Update(seconds, feature_->BackgroundMotion(), active))
        {
            pending_.reset();
            static_cast<void>(feature_->TakeCommand());
            return std::nullopt;
        }
        lastUpdateSucceeded_ = true;
        environment.CommitAudioCommands();
        if (revision_ != environment.SceneRevision())
            Refresh(environment);
        else
            feature_->ApplyPresentation(environment);
        if (auto command = feature_->TakeCommand())
            pending_ = std::move(command);
        auto result = std::move(pending_);
        pending_.reset();
        if (result && result->action == "loadScene")
        {
            std::string error;
            if (environment.LoadScene(result->target, false, error))
                Refresh(environment);
            else
            {
                Engine::Log::Error(error);
                result.reset();
            }
        }
        return result;
    }

    auto GamePlayback::Suspend(SceneEnvironment& environment) -> void
    {
        feature_->Suspend();
        environment.DiscardPendingInput();
        environment.UpdateAudio(false);
    }

    auto GamePlayback::Pointer(SceneEnvironment& environment, unsigned int width, unsigned int height,
                               UiPointerInput input, bool active) -> UiEvent
    {
        auto& state = environment.Ui();
        if (!active)
        {
            pointerReady_ = pointerDown_ = false;
            state.hovered.clear();
            state.pressed.clear();
            state.focused.clear();
            return {};
        }
        const bool down = input.down;
        input.pressed = pointerReady_ && down && !pointerDown_;
        input.released = pointerReady_ && !down && pointerDown_;
        input.down = pointerReady_ && down;
        pointerDown_ = down;
        pointerReady_ = true;
        if (feature_->BlocksPointer())
        {
            state.hovered.clear();
            state.pressed.clear();
            return {};
        }
        const auto hovered = SceneUi::Hit(environment.World().Layout(), width, height, input.x, input.y, state);
        if (hovered != state.hovered)
            feature_->SelectHovered(environment.World().Layout(), hovered, state);
        const auto event = environment.UiPointer(width, height, input.x, input.y, input.down, input.pressed,
                                                 input.released, input.wheel);
        if (!feature_->Activate(event, state) && (event.action == "loadScene" || event.action == "quit"))
            pending_ = event;
        if (auto command = feature_->TakeCommand())
            pending_ = std::move(command);
        return {};
    }

    auto GamePlayback::Shortcut(SceneEnvironment& environment, const Engine::InputActions& actions, unsigned int width,
                                unsigned int height, bool active) -> UiEvent
    {
        if (!active || feature_->Active() || !environment.Ui().focused.empty())
            return {};
        for (const auto& [name, binding] : actions.GetBindings())
        {
            static_cast<void>(binding);
            if (!actions.Pressed(name))
                continue;
            const auto object =
                SceneUi::Shortcut(environment.World().Layout(), "action:" + name, width, height, environment.Ui());
            if (!object.empty())
            {
                const auto event = environment.Click(object);
                if (event.action == "loadScene" || event.action == "quit")
                    pending_ = event;
                return {};
            }
        }
        return {};
    }

    auto GamePlayback::Refresh(SceneEnvironment& environment) -> void
    {
        pending_.reset();
        pointerReady_ = pointerDown_ = false;
        revision_ = environment.SceneRevision();
        feature_->Initialize(environment, settingsPath_, playIntro_);
        environment.Ui().values["volumeGain"] = GameSettings::Load(settingsPath_).Gain();
        feature_->ApplyAnimation(environment);
    }
}
