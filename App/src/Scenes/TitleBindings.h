#pragma once
#include "TitleMenu.h"
#include <SceneRuntime/SceneUi.h>
#include <optional>
#include <algorithm>

namespace App
{
    struct TitleBindings
    {
        static std::optional<TitleMenuItem> Item(const SceneRuntime::ScenePlacement& object)
        {
            if(!object.button || !object.button->enabled) return std::nullopt;
            const auto& event=object.button->event;
            if(event=="menu:0" || event=="start") return TitleMenuItem::Start;
            if(event=="menu:1") return TitleMenuItem::Settings;
            if(event=="menu:2") return TitleMenuItem::Exit;
            return std::nullopt;
        }
        static std::vector<TitleMenuItem> Items(const SceneRuntime::SceneLayout& layout)
        {
            std::vector<TitleMenuItem> result;
            auto state=SceneRuntime::SceneUi::Defaults(layout);
            state.values["screen"]=0; state.values["intro"]=1;
            for(const auto& object:layout.objects) {
                const auto item=Item(object);
                if(item && SceneRuntime::SceneUi::Resolve(layout,object,1280,720,state).visible &&
                    std::find(result.begin(),result.end(),*item)==result.end()) result.push_back(*item);
            }
            return result;
        }
        static std::string StartScene(const SceneRuntime::SceneLayout& layout)
        {
            auto state=SceneRuntime::SceneUi::Defaults(layout);
            state.values["screen"]=0; state.values["intro"]=1;
            for(const auto& object:layout.objects)
                if(Item(object)==TitleMenuItem::Start && SceneRuntime::SceneUi::Resolve(layout,object,1280,720,state).visible) {
                    const auto& target=object.button->target;
                    if(target.starts_with("Assets/Scenes/") && target.ends_with(".json") &&
                        target.find("..") == std::string::npos && target.find('\\')==std::string::npos) return target;
                }
            return {};
        }
    };
}
