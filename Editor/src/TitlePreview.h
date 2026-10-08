#pragma once
#include "../../App/src/Scenes/TitleBindings.h"
#include "../../App/src/Scenes/TitleUi.h"
#include <Engine/Input/InputActions.h>
#include <optional>

namespace Editor
{
    class TitlePreview final
    {
    public:
        void Initialize(const SceneRuntime::SceneLayout& layout,SceneRuntime::UiState& state)
        {
            menu_.reset(); pending_.reset(); startScene_.clear();
            const auto items=App::TitleBindings::Items(layout);
            if (items.empty()) return;
            menu_.emplace(); menu_->SetItems(items);
            menu_->SetTransitionDuration(state.Value("startDuration",0.32f));
            menu_->SetPresentationDurations(state.Value("introDuration",0.65f),state.Value("selectionDuration",0.16f));
            startScene_=App::TitleBindings::StartScene(layout);
            Sync(state);
        }
        bool Active() const { return menu_.has_value(); }
        bool BackgroundMotion() const { return !menu_ || menu_->GetSettings().backgroundMotion; }
        bool Activate(const SceneRuntime::UiEvent& event,SceneRuntime::UiState& state)
        {
            if (!menu_ || !(event.event.starts_with("menu:") || event.event.starts_with("settings:") ||
                event.event=="back" || event.event=="volume" || event.event=="start")) return false;
            Action(menu_->ActivateUi(event.event)); Sync(state); return true;
        }
        std::optional<SceneRuntime::UiEvent> Update(const Engine::InputActions& actions,bool active,double seconds,SceneRuntime::UiState& state)
        {
            if (!menu_) return std::nullopt;
            App::TitleMenuInput input; input.active=active;
            const std::pair<const char*,unsigned int> bindings[]={{"MoveForward",App::MenuUp},{"MoveBack",App::MenuDown},
                {"MoveLeft",App::MenuLeft},{"MoveRight",App::MenuRight},{"Confirm",App::MenuConfirm},{"Cancel",App::MenuBack}};
            for (const auto& [name,button] : bindings) if (active && actions.Down(name)) input.keyboardButtons|=button;
            Action(menu_->Update(input,seconds)); Sync(state);
            auto result=std::move(pending_); pending_.reset(); return result;
        }
    private:
        void Action(App::TitleMenuAction action)
        {
            if (action==App::TitleMenuAction::SaveSettings) menu_->CompleteSave(true);
            if (action==App::TitleMenuAction::Start && !startScene_.empty())
            { SceneRuntime::UiEvent event; event.action="loadScene"; event.target=startScene_; pending_=std::move(event); }
            if (action==App::TitleMenuAction::Exit)
            { SceneRuntime::UiEvent event; event.action="quit"; pending_=std::move(event); }
        }
        void Sync(SceneRuntime::UiState& state)
        { for (const auto& [name,value] : App::TitleUi::State(*menu_,state).values) state.values[name]=value; }
        std::optional<App::TitleMenu> menu_;
        std::optional<SceneRuntime::UiEvent> pending_;
        std::string startScene_;
    };
}
