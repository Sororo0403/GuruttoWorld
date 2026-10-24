#pragma once
#include <iterator>
#include <SceneRuntime/Gameplay/TitleMenu.h>
#include <SceneRuntime/Ui/SceneUi.h>
#include <optional>
#include <algorithm>
#include <charconv>
#include <Engine/Input/InputActions.h>

namespace SceneRuntime
{
    struct TitleBindings
    {
        static float AnalogValue(const Engine::InputBinding& binding,const Engine::InputSnapshot* snapshot)
        {
            float analog=0;const int axis=std::abs(binding.axis);
            if(axis==1 || axis==2) analog=snapshot->stick[axis-1];
            if(axis==3 || axis==4) analog=snapshot->rightStick[axis-3];
            if(axis==5 || axis==6) analog=snapshot->triggers[axis-5];
            if(binding.axis<0) analog=-analog;
            return analog;
        }

        static void ApplyDeviceBinding(TitleMenuInput& input,const Engine::InputBinding& binding,const Engine::InputSnapshot* snapshot,unsigned int button)
        {
            const bool keyboard=std::any_of(binding.keys.begin(),binding.keys.end(),[&](unsigned int key){return snapshot->down[key];}) ||
                (binding.mouseButtons && (snapshot->mouseButtons&binding.mouseButtons)==binding.mouseButtons);
            if(keyboard) input.keyboardButtons|=button;
            const float analog=AnalogValue(binding,snapshot);
            if(snapshot->gamepadConnected && ((binding.buttons && (snapshot->buttons&binding.buttons)==binding.buttons) ||
                (binding.axis && std::isfinite(analog) && analog>=binding.threshold))) input.gamepadButtons|=button;
        }

        static TitleMenuInput Input(const SceneRuntime::MenuConfiguration& config,const Engine::InputActions& actions,bool active,const Engine::InputSnapshot* snapshot=nullptr) {
            TitleMenuInput input; input.active=active;
            constexpr unsigned int buttons[]{MenuUp,MenuDown,MenuLeft,MenuRight,MenuConfirm,MenuBack};
            for(size_t i=0;i<config.inputs.size();++i) if(active && actions.Down(config.inputs[i])) input.keyboardButtons|=buttons[i];
            input.anyButtonPressed=active && std::any_of(actions.PressedValues().begin(),actions.PressedValues().end(),[](const auto& pair){return pair.second;});
            if(snapshot && active) {
                input.keyboardButtons=0;input.gamepadConnected=snapshot->gamepadConnected;
                input.anyButtonPressed=std::any_of(snapshot->pressed.begin(),snapshot->pressed.end(),[](bool value){return value;}) || snapshot->pressedButtons!=0;
                for(size_t i=0;i<config.inputs.size();++i) {
                    if(!actions.Down(config.inputs[i])) continue;
                    const auto found=actions.GetBindings().find(config.inputs[i]);if(found==actions.GetBindings().end()) continue;
                    ApplyDeviceBinding(input,found->second,snapshot,buttons[i]);
                }
            }
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
        static const SceneRuntime::ScenePlacement* MenuCanvas(const SceneRuntime::SceneLayout& layout) {
            const auto found=std::find_if(layout.objects.begin(),layout.objects.end(),[](const auto& object) {
                return object.canvas && object.canvas->enabled && object.canvas->menu;
            });
            return found==layout.objects.end()?nullptr:&*found;
        }
        static const SceneRuntime::MenuConfiguration* Configuration(const SceneRuntime::SceneLayout& layout) {
            const auto* object=MenuCanvas(layout);
            return object?&*object->canvas->menu:nullptr;
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
            if(const auto* object=MenuCanvas(layout)) return {static_cast<unsigned int>(object->canvas->referenceSize[0]),static_cast<unsigned int>(object->canvas->referenceSize[1])};
            return {1280,720};
        }

    };
}
