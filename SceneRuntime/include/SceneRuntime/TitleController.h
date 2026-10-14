#pragma once
#include <SceneRuntime/TitleBindings.h>
#include <SceneRuntime/TitleUi.h>
#include <Engine/Input/InputActions.h>
#include <SceneRuntime/SceneEnvironment.h>
#include <optional>
#include <SceneRuntime/TitleConfigTransition.h>

namespace SceneRuntime
{
    class TitleController final
    {
    public:
        void Initialize(const SceneRuntime::SceneLayout& layout,SceneRuntime::UiState& state,const std::filesystem::path& settingsPath={},bool playIntro=false)
        {
            configTransition_={}; menu_.reset(); pending_.reset(); departureFrom_.reset(); fade_=0; pendingCue_=0; settingsPath_=settingsPath;seekIntro_=false;
            const auto* configuration=SceneRuntime::TitleBindings::Configuration(layout);
            if(!configuration) return;
            const auto items=SceneRuntime::TitleBindings::Items(layout);
            if (items.empty() && !configuration->pressAnyButton && !configuration->settingsScreen) return;
            menu_.emplace(playIntro && !configuration->settingsScreen); menu_->Configure(*configuration);
            if(configuration->settingsScreen) {
                std::vector<TitleMenuItem> settingsItems;for(const auto& entry:configuration->entries) settingsItems.push_back(static_cast<TitleMenuItem>(entry.index));
                menu_->SetItems(settingsItems);
            } else menu_->SetItems(items);
            configTransition_.Configure(layout,configuration->configTransition,state,configuration->settingsScreen);
            const auto settings=SceneRuntime::GameSettings::Load(settingsPath_); if(settings.loaded) menu_->LoadSettings(settings);
            menu_->SetTransitionDuration(state.Value("startDuration",0.32f));
            menu_->SetPresentationDurations(state.Value("introDuration",0.65f),state.Value("selectionDuration",0.16f));
            if(configuration->settingsScreen) {
                for(const auto& entry:configuration->entries) if(entry.action=="settings") {menu_->ActivateUi("menu:"+std::to_string(entry.index));break;}
            }
            seekIntro_=menu_->IntroProgress()==1;
            Sync(state);
        }
        void PreparePresentation(SceneEnvironment& environment,double seconds,bool active) {
            configTransition_.Prepare(environment,seconds,active);
            SyncDeparture(environment.Ui());
            if(!active || !Departing() || departureFrom_) return;
            const auto* camera=SceneView::CameraObject(environment.World().Layout());
            ScenePlacement pose;DirectX::XMFLOAT4X4 matrix;
            if(camera && environment.World().WorldMatrix(camera->id,matrix) && SceneTransforms::ReadTransform(matrix,{},pose)) departureFrom_=pose;
        }
        void ApplyPresentation(SceneEnvironment& environment) {
            configTransition_.Apply(environment);
            if(!menu_) return;
            if(Departing() && departureFrom_) {
                const auto* camera=SceneView::CameraObject(environment.World().Layout());
                ScenePlacement target;DirectX::XMFLOAT4X4 matrix;
                if(camera && environment.World().WorldMatrix(menu_->SelectedEntry()->transitionCameraTarget,matrix) && SceneTransforms::ReadTransform(matrix,{},target)) {
                    // Ease into the road/exit direction from the actual current camera, including unfinished selection motion.
                    const float approach=std::clamp(environment.Ui().Value(menu_->SelectedEntry()->action+"ApproachFraction",1),.05f,1.0f);
                    const float p=std::min(1.0f,menu_->TransitionProgress()/approach);const float blend=p*p*(3-2*p);
                    for(size_t axis=0;axis<3;++axis) {
                        target.position[axis]=departureFrom_->position[axis]+(target.position[axis]-departureFrom_->position[axis])*blend;
                        target.rotation[axis]=departureFrom_->rotation[axis]+std::remainder(target.rotation[axis]-departureFrom_->rotation[axis],6.283185307f)*blend;
                    }
                    if(!environment.World().SetWorldTransform(camera->id,target.position,target.rotation,target.scale)) Engine::Log::Warning("Departure camera transform was rejected");
                } else Engine::Log::Warning("Departure camera target is missing or invalid");
            }
            SyncDeparture(environment.Ui());
            if(configTransition_.TakeEntered()) {
                const auto* entry=menu_->SelectedEntry();
                if(entry && entry->action=="settings" && !entry->target.empty() && !menu_->Configuration().settingsScreen)
                    QueueScene(entry->target);
            }
            if(configTransition_.TakeReturned() && menu_->Configuration().settingsScreen) QueueScene(menu_->Configuration().returnScene);
        }
        void AdvancePresentation(SceneEnvironment& environment,double seconds,bool active) {PreparePresentation(environment,seconds,active);environment.World().Animate(environment.Ui().values);ApplyPresentation(environment);}
        std::optional<UiEvent> TakeCommand() {auto result=std::move(pending_);pending_.reset();return result;}
        bool BlocksPointer() const { return configTransition_.Blocking() || (menu_ && menu_->IsTransitioning()); }
        void SelectHovered(const SceneLayout& layout,const std::string& id,UiState& state)
        {
            if(!menu_ || BlocksPointer()) return;
            for(const auto& object:layout.objects) if(object.id==id) {
                if(const auto item=TitleBindings::Item(object)) {menu_->SelectUi(*item); Sync(state);}
                break;
            }
        }
        bool Active() const { return menu_.has_value(); }
        void Suspend() { if(menu_) menu_->Update({}); }
        bool BackgroundMotion() const { return !menu_ || menu_->GetSettings().backgroundMotion; }
        void ApplyAnimation(SceneRuntime::SceneEnvironment& environment) {
            if(seekIntro_) {environment.SeekAnimation(environment.Ui().Value("introDuration",0),static_cast<float>(environment.MotionSeconds()));seekIntro_=false;}
        }
        void UpdateAudio(SceneRuntime::SceneEnvironment& environment,double seconds,bool active) {
            if(!menu_) return;
            if(active && std::isfinite(seconds)) fade_=std::min(1.0f,fade_+static_cast<float>(std::clamp(seconds,0.0,0.1))/std::clamp(environment.Ui().Value("musicFadeDuration",.4f),.01f,10.0f));
            const float gain=menu_->GetSettings().Gain();
            environment.Ui().values["volumeGain"]=gain;
            environment.Ui().values["musicVolume"]=gain*fade_*(1-menu_->TransitionProgress());
            const auto cue=pendingCue_; pendingCue_=0;
            if(active && gain>0 && cue) environment.AudioCue(menu_->Configuration().cues[cue-1]);
        }
        bool Activate(const SceneRuntime::UiEvent& event,SceneRuntime::UiState& state)
        {
            if (!menu_ || !(event.event.starts_with("menu:") || event.event.starts_with("settings:") ||
                event.event=="back" || event.event=="volume" || event.event=="start")) return false;
            if(BlocksPointer()) return true;
            const float intro=menu_->IntroProgress();
            Action(menu_->ActivateUi(event.event)); seekIntro_=seekIntro_ || (intro<1 && menu_->IntroProgress()==1);Sync(state); return true;
        }
        std::optional<SceneRuntime::UiEvent> Update(const Engine::InputActions& actions,bool active,double seconds,SceneRuntime::UiState& state,const Engine::InputSnapshot* snapshot=nullptr)
        {
            if (!menu_) return std::nullopt;
            auto input=SceneRuntime::TitleBindings::Input(menu_->Configuration(),actions,active,snapshot);
            if(configTransition_.BlockInput(input,snapshot && (snapshot->mouseButtons&1)!=0)) {input.keyboardButtons=0; input.gamepadButtons=0; input.stickX=input.stickY=0;}
            const float intro=menu_->IntroProgress();
            Action(menu_->Update(input,seconds));seekIntro_=seekIntro_ || (intro<1 && menu_->IntroProgress()==1); Sync(state);
            auto result=std::move(pending_); pending_.reset(); return result;
        }
    private:
        bool Departing() const {
            return menu_ && menu_->IsTransitioning() && menu_->SelectedEntry() && !menu_->SelectedEntry()->transitionCameraTarget.empty();
        }
        void SyncDeparture(UiState& state) const {
            state.values["departureKind"]=-1;
            state.values["titleEffectTime"]=state.Value("configEnterTime");
            if(!Departing()) return;
            state.values["departureKind"]=menu_->SelectedEntry()->action=="quit" ? 1.0f : 0.0f;
            state.values["titleEffectTime"]=menu_->TransitionProgress()*2;
            state.values["screen"]=3;
        }
        void QueueScene(const std::string& scene) {if(!scene.empty()) {UiEvent event;event.action="loadScene";event.target=scene;pending_=std::move(event);}}
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
            if(menu_->GetCue()==SceneRuntime::TitleMenuCue::Confirm && !menu_->IsSettingsOpen() && menu_->SelectedEntry() && menu_->SelectedEntry()->action=="loadScene") {
                state.values["startRequested"]=1;
            }
            std::string assignments; if(menu_->TakeAssignments(assignments)) state.Assign(assignments);
            for (const auto& [name,value] : SceneRuntime::TitleUi::State(*menu_,state).values) state.values[name]=value;
            configTransition_.Observe(menu_->IsSettingsOpen(),state);
            SyncDeparture(state);
            if(menu_->Configuration().settingsScreen && !menu_->IsSettingsOpen() && !configTransition_.Enabled()) QueueScene(menu_->Configuration().returnScene);
            if(!menu_->Configuration().settingsScreen && menu_->IsSettingsOpen() && !configTransition_.Enabled()) {
                const auto* entry=menu_->SelectedEntry();if(entry && entry->action=="settings") QueueScene(entry->target);
            }
        }
        SceneRuntime::TitleConfigTransition configTransition_;
        std::optional<SceneRuntime::TitleMenu> menu_;
        std::optional<SceneRuntime::UiEvent> pending_;
        std::optional<ScenePlacement> departureFrom_;
        std::filesystem::path settingsPath_;
        bool seekIntro_=false;
        float fade_=0;
        size_t pendingCue_=0;
    };
}
