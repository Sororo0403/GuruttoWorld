#pragma once
#include <SceneRuntime/TitleBindings.h>
#include <SceneRuntime/TitleUi.h>
#include <Engine/Input/InputActions.h>
#include <SceneRuntime/SceneEnvironment.h>
#include <optional>
#include <SceneRuntime/TitleConfigTransition.h>

namespace Editor
{
    class TitlePreview final
    {
    public:
        void Initialize(const SceneRuntime::SceneLayout& layout,SceneRuntime::UiState& state,const std::filesystem::path& settingsPath={},bool playIntro=false)
        {
            configTransition_={}; menu_.reset(); pending_.reset(); fade_=0; pendingCue_=0; settingsPath_=settingsPath;seekIntro_=false;
            const auto* configuration=SceneRuntime::TitleBindings::Configuration(layout);
            if(!configuration) return;
            const auto items=SceneRuntime::TitleBindings::Items(layout);
            if (items.empty() && !configuration->pressAnyButton) return;
            menu_.emplace(playIntro); menu_->Configure(*configuration); menu_->SetItems(items);
            const auto settings=SceneRuntime::GameSettings::Load(settingsPath_); if(settings.loaded) menu_->LoadSettings(settings);
            menu_->SetTransitionDuration(state.Value("startDuration",0.32f));
            menu_->SetPresentationDurations(state.Value("introDuration",0.65f),state.Value("selectionDuration",0.16f));
            seekIntro_=menu_->IntroProgress()==1;
            Sync(state);
        }
        void AdvancePresentation(SceneRuntime::SceneEnvironment& environment,double seconds,bool active) { configTransition_.Advance(environment,seconds,active); }
        bool Active() const { return menu_.has_value(); }
        void Suspend() { if(menu_) menu_->Update({}); }
        bool BackgroundMotion() const { return !menu_ || menu_->GetSettings().backgroundMotion; }
        void ApplyAnimation(SceneRuntime::SceneEnvironment& environment) {
            if(seekIntro_) {environment.SeekAnimation(environment.Ui().Value("introDuration",0),static_cast<float>(environment.MotionSeconds()));seekIntro_=false;}
        }
        void UpdateAudio(SceneRuntime::SceneEnvironment& environment,double seconds,bool active) {
            if(!menu_) return;
            if(active) fade_=std::min(1.0f,fade_+static_cast<float>(std::clamp(seconds,0.0,0.1))/std::clamp(environment.Ui().Value("musicFadeDuration",.4f),.01f,10.0f));
            const float gain=menu_->GetSettings().Gain();
            environment.Ui().values["volumeGain"]=gain;
            environment.Ui().values["musicVolume"]=gain*fade_*(1-menu_->TransitionProgress());
            const auto cue=pendingCue_; pendingCue_=0;
            if(active && cue) environment.AudioCue(menu_->Configuration().cues[cue-1]);
        }
        bool Activate(const SceneRuntime::UiEvent& event,SceneRuntime::UiState& state)
        {
            if (!menu_ || !(event.event.starts_with("menu:") || event.event.starts_with("settings:") ||
                event.event=="back" || event.event=="volume" || event.event=="start")) return false;
            if(configTransition_.Blocking()) return true;
            const float intro=menu_->IntroProgress();
            Action(menu_->ActivateUi(event.event)); seekIntro_=seekIntro_ || (intro<1 && menu_->IntroProgress()==1);Sync(state); return true;
        }
        std::optional<SceneRuntime::UiEvent> Update(const Engine::InputActions& actions,bool active,double seconds,SceneRuntime::UiState& state)
        {
            if (!menu_) return std::nullopt;
            auto input=SceneRuntime::TitleBindings::Input(menu_->Configuration(),actions,active);
            if(configTransition_.BlockInput(input)) {input.keyboardButtons=0; input.gamepadButtons=0; input.stickX=input.stickY=0;}
            const float intro=menu_->IntroProgress();
            Action(menu_->Update(input,seconds));seekIntro_=seekIntro_ || (intro<1 && menu_->IntroProgress()==1); Sync(state);
            auto result=std::move(pending_); pending_.reset(); return result;
        }
    private:
        void Action(SceneRuntime::TitleMenuAction action)
        {
            if (action==SceneRuntime::TitleMenuAction::SaveSettings) menu_->CompleteSave(menu_->GetSettings().Save(settingsPath_));
            if (action==SceneRuntime::TitleMenuAction::Start && !menu_->PendingTarget().empty())
            { SceneRuntime::UiEvent event; event.action="loadScene"; event.target=menu_->PendingTarget(); pending_=std::move(event); }
            if (action==SceneRuntime::TitleMenuAction::Exit)
            { SceneRuntime::UiEvent event; event.action="quit"; pending_=std::move(event); }
        }
        void Sync(SceneRuntime::UiState& state)
        {
            if(menu_->GetCue()!=SceneRuntime::TitleMenuCue::None) pendingCue_=static_cast<size_t>(menu_->GetCue());
            if(menu_->GetCue()==SceneRuntime::TitleMenuCue::Confirm && !menu_->IsSettingsOpen() && menu_->SelectedEntry() && menu_->SelectedEntry()->action=="loadScene") state.values["startRequested"]=1;
            std::string assignments; if(menu_->TakeAssignments(assignments)) state.Assign(assignments);
            for (const auto& [name,value] : SceneRuntime::TitleUi::State(*menu_,state).values) state.values[name]=value;
            configTransition_.Observe(menu_->IsSettingsOpen(),state);
        }
        SceneRuntime::TitleConfigTransition configTransition_;
        std::optional<SceneRuntime::TitleMenu> menu_;
        std::optional<SceneRuntime::UiEvent> pending_;
        std::filesystem::path settingsPath_;
        bool seekIntro_=false;
        float fade_=0;
        size_t pendingCue_=0;
    };
}
