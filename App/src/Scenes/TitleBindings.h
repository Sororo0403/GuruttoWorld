#pragma once
#include "TitleMenu.h"
#include <SceneRuntime/SceneUi.h>
#include <optional>
#include <algorithm>
#include <charconv>
#include <Engine/Input/InputActions.h>

namespace App
{
    struct TitleBindings
    {
        static TitleMenuInput Input(const SceneRuntime::MenuConfiguration& config,const Engine::InputActions& actions,bool active) {
            TitleMenuInput input; input.active=active;
            constexpr unsigned int buttons[]{MenuUp,MenuDown,MenuLeft,MenuRight,MenuConfirm,MenuBack};
            for(size_t i=0;i<config.inputs.size();++i) if(active && actions.Down(config.inputs[i])) input.keyboardButtons|=buttons[i];
            input.anyButtonPressed=active && std::any_of(actions.PressedValues().begin(),actions.PressedValues().end(),[](const auto& pair){return pair.second;});
            return input;
        }
        static std::optional<TitleMenuItem> Item(const SceneRuntime::ScenePlacement& object)
        {
            if(!object.button || !object.button->enabled) return std::nullopt;
            const auto& event=object.button->event;
            if(event=="start") return TitleMenuItem::Start;
            if(event.starts_with("menu:")) {
                int index=0; const auto digits=std::string_view(event).substr(5);
                const auto result=std::from_chars(digits.data(),digits.data()+digits.size(),index);
                if(result.ec==std::errc{} && result.ptr==digits.data()+digits.size() && index>=0 && index<128) return static_cast<TitleMenuItem>(index);
            }
            return std::nullopt;
        }
        static const SceneRuntime::MenuConfiguration* Configuration(const SceneRuntime::SceneLayout& layout) {
            for(const auto& object:layout.objects) if(object.canvas && object.canvas->enabled && object.canvas->menu) return &*object.canvas->menu;
            return nullptr;
        }
        static SceneRuntime::MenuConfiguration EffectiveConfiguration(const SceneRuntime::SceneLayout& layout) {
            if(const auto* config=Configuration(layout)) return *config;
            auto config=SceneRuntime::DefaultMenuConfiguration();
            config.entries[0].target=StartScene(layout);
            return config;
        }
        static std::vector<TitleMenuItem> Items(const SceneRuntime::SceneLayout& layout)
        {
            std::vector<TitleMenuItem> result;
            auto state=SceneRuntime::SceneUi::Defaults(layout);
            state.values["screen"]=0; state.values["intro"]=1;
            for(const auto& object:layout.objects) {
                const auto item=Item(object);
                if(item && SceneRuntime::SceneUi::Resolve(layout,object,ReferenceSize(layout)[0],ReferenceSize(layout)[1],state).visible &&
                    std::find(result.begin(),result.end(),*item)==result.end()) result.push_back(*item);
            }
            if(const auto* config=Configuration(layout)) std::erase_if(result,[&](auto item){return std::none_of(config->entries.begin(),config->entries.end(),[&](const auto& e){return e.index==static_cast<int>(item);});});
            return result;
        }
        static std::array<unsigned int,2> ReferenceSize(const SceneRuntime::SceneLayout& layout) {
            for(const auto& o:layout.objects) if(o.canvas && o.canvas->enabled && o.canvas->menu) return {static_cast<unsigned int>(o.canvas->referenceSize[0]),static_cast<unsigned int>(o.canvas->referenceSize[1])};
            return {1280,720};
        }
        static std::string StartScene(const SceneRuntime::SceneLayout& layout)
        {
            auto state=SceneRuntime::SceneUi::Defaults(layout);
            state.values["screen"]=0; state.values["intro"]=1;
            for(const auto& object:layout.objects)
                if(Item(object)==TitleMenuItem::Start && SceneRuntime::SceneUi::Resolve(layout,object,ReferenceSize(layout)[0],ReferenceSize(layout)[1],state).visible) {
                    const auto& target=object.button->target;
                    if(target.starts_with("Assets/Scenes/") && target.ends_with(".json") &&
                        target.find("..") == std::string::npos && target.find('\\')==std::string::npos) return target;
                }
            return {};
        }
    };
}
