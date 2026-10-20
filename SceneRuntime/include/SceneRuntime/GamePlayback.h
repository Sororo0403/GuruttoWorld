#pragma once
#include <SceneRuntime/TitleController.h>
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
        void Initialize(SceneEnvironment& environment, const std::filesystem::path& root,
                        const std::filesystem::path& settingsPath = {}, bool playIntro = true)
        {
            root_ = root;
            settingsPath_ = settingsPath.empty() ? GameSettings::UserPath(*environment.Content()) : settingsPath;
            playIntro_ = playIntro;
            pending_.reset();
            Refresh(environment);
            std::string error;
            if (!environment.StartAudio(root, error))
                Engine::Log::Warning(error);
            environment.Ui().values["volumeGain"] = GameSettings::Load(settingsPath_).Gain();
            title_.ApplyAnimation(environment);
        }
        std::optional<UiEvent> PrepareInput(SceneEnvironment& environment, const Engine::InputActions& actions,
                                            bool acceptsInput, double seconds,
                                            const Engine::InputSnapshot* snapshot = nullptr)
        {
            if (revision_ != environment.SceneRevision())
                Refresh(environment);
            const bool input = acceptsInput && environment.Ui().focused.empty();
            auto command = title_.Update(actions, input, seconds, environment.Ui(), snapshot);
            const bool gameplay = input && !title_.Active();
            environment.SetInputActions(gameplay ? actions.Values() : std::map<std::string, float>{},
                                        gameplay ? actions.PressedValues() : std::map<std::string, bool>{});
            const auto& mapping = environment.World().Layout().settings.playerInputs;
            environment.MovePlayers(seconds, gameplay ? actions.Value(mapping[0]) - actions.Value(mapping[1]) : 0,
                                    gameplay ? actions.Value(mapping[2]) - actions.Value(mapping[3]) : 0,
                                    gameplay && actions.Pressed(mapping[4]));
            if (command)
                pending_ = command;
            return std::nullopt;
        }
        /// <summary>最後の更新でWorldと副作用予約が確定したかを返します。</summary>
        bool LastUpdateSucceeded() const
        {
            return lastUpdateSucceeded_;
        }
        std::optional<UiEvent> Update(SceneEnvironment& environment, double seconds, bool active)
        {
            lastUpdateSucceeded_ = false;
            if (!active)
            {
                Suspend(environment);
                return std::nullopt;
            }
            if (revision_ != environment.SceneRevision())
                Refresh(environment);
            title_.ApplyAnimation(environment);
            title_.UpdateAudio(environment, seconds, active);
            title_.PreparePresentation(environment, seconds, active);
            if (!environment.Update(seconds, title_.BackgroundMotion(), active))
            {
                pending_.reset();
                static_cast<void>(title_.TakeCommand());
                return std::nullopt;
            }
            lastUpdateSucceeded_ = true;
            environment.CommitAudioCommands();
            if (revision_ != environment.SceneRevision())
                Refresh(environment);
            else
                title_.ApplyPresentation(environment);
            if (auto command = title_.TakeCommand())
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
        void Suspend(SceneEnvironment& environment)
        {
            title_.Suspend();
            environment.DiscardPendingInput();
            environment.UpdateAudio(false);
        }
        UiEvent Pointer(SceneEnvironment& environment, unsigned int width, unsigned int height, UiPointerInput input,
                        bool active)
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
            if (title_.BlocksPointer())
            {
                state.hovered.clear();
                state.pressed.clear();
                return {};
            }
            const auto hovered = SceneUi::Hit(environment.World().Layout(), width, height, input.x, input.y, state);
            if (hovered != state.hovered)
                title_.SelectHovered(environment.World().Layout(), hovered, state);
            const auto event = environment.UiPointer(width, height, input.x, input.y, input.down, input.pressed,
                                                     input.released, input.wheel);
            if (!title_.Activate(event, state) && (event.action == "loadScene" || event.action == "quit"))
                pending_ = event;
            if (auto command = title_.TakeCommand())
                pending_ = std::move(command);
            return {};
        }
        UiEvent Shortcut(SceneEnvironment& environment, const Engine::InputActions& actions, unsigned int width,
                         unsigned int height, bool active)
        {
            if (!active || title_.Active() || !environment.Ui().focused.empty())
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

    private:
        void Refresh(SceneEnvironment& environment)
        {
            pending_.reset();
            pointerReady_ = pointerDown_ = false;
            revision_ = environment.SceneRevision();
            const auto* canvas = TitleBindings::MenuCanvas(environment.World().Layout());
            const bool intro = canvas && canvas->canvas->menu && !canvas->canvas->menu->settingsScreen && playIntro_ &&
                               environment.FirstTitleVisit(canvas->name);
            title_.Initialize(environment.World().Layout(), environment.Ui(), settingsPath_, intro);
            environment.Ui().values["volumeGain"] = GameSettings::Load(settingsPath_).Gain();
            title_.ApplyAnimation(environment);
        }
        bool lastUpdateSucceeded_ = false;
        std::filesystem::path root_, settingsPath_;
        uint64_t revision_ = 0;
        bool playIntro_ = true;
        std::optional<UiEvent> pending_;
        TitleController title_;
        bool pointerReady_ = false, pointerDown_ = false;
    };
}
