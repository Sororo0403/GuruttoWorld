#pragma once
#include "../../App/src/Scenes/TitleBindings.h"
#include "../../App/src/Scenes/TitleUi.h"
#include <Engine/Input/InputActions.h>
#include <SceneRuntime/SceneEnvironment.h>
#include <optional>

namespace Editor
{
    class TitlePreview final
    {
    public:
        void Initialize(const SceneRuntime::SceneLayout& layout,SceneRuntime::UiState& state)
        {
            menu_.reset(); pending_.reset(); fade_=0; pendingCue_=0;
            const auto* configuration=App::TitleBindings::Configuration(layout);
            if(!configuration) return;
            const auto items=App::TitleBindings::Items(layout);
            if (items.empty() && !configuration->pressAnyButton) return;
            menu_.emplace(); menu_->Configure(*configuration); menu_->SetItems(items);
            menu_->SetTransitionDuration(state.Value("startDuration",0.32f));
            menu_->SetPresentationDurations(state.Value("introDuration",0.65f),state.Value("selectionDuration",0.16f));
            Sync(state);
        }
        bool Active() const { return menu_.has_value(); }
        void Suspend() { if(menu_) menu_->Update({}); }
        bool BackgroundMotion() const { return !menu_ || menu_->GetSettings().backgroundMotion; }
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
            Action(menu_->ActivateUi(event.event)); Sync(state); return true;
        }
        std::optional<SceneRuntime::UiEvent> Update(const Engine::InputActions& actions,bool active,double seconds,SceneRuntime::UiState& state)
        {
            if (!menu_) return std::nullopt;
            const auto input=App::TitleBindings::Input(menu_->Configuration(),actions,active);
            Action(menu_->Update(input,seconds)); Sync(state);
            auto result=std::move(pending_); pending_.reset(); return result;
        }
    private:
        void Action(App::TitleMenuAction action)
        {
            if (action==App::TitleMenuAction::SaveSettings) menu_->CompleteSave(true);
            if (action==App::TitleMenuAction::Start && !menu_->PendingTarget().empty())
            { SceneRuntime::UiEvent event; event.action="loadScene"; event.target=menu_->PendingTarget(); pending_=std::move(event); }
            if (action==App::TitleMenuAction::Exit)
            { SceneRuntime::UiEvent event; event.action="quit"; pending_=std::move(event); }
        }
        void Sync(SceneRuntime::UiState& state)
        {
            if(menu_->GetCue()!=App::TitleMenuCue::None) pendingCue_=static_cast<size_t>(menu_->GetCue());
            if(menu_->GetCue()==App::TitleMenuCue::Confirm && !menu_->IsSettingsOpen() && menu_->SelectedEntry() && menu_->SelectedEntry()->action=="loadScene") state.values["startRequested"]=1;
            std::string assignments; if(menu_->TakeAssignments(assignments)) state.Assign(assignments);
            for (const auto& [name,value] : App::TitleUi::State(*menu_,state).values) state.values[name]=value;
        }
        std::optional<App::TitleMenu> menu_;
        std::optional<SceneRuntime::UiEvent> pending_;
        float fade_=0;
        size_t pendingCue_=0;
    };
}
