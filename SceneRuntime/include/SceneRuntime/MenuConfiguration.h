#pragma once
#include <string>
#include <Engine/Core/Validation.h>
#include <vector>
#include <array>
#include <stdexcept>
#include <set>
#include <cmath>
#include <map>
#include <algorithm>

namespace SceneRuntime {
struct MenuEntry {
    int index=0;
    std::string action="loadScene", target;
    float focus=0, view=0;
    std::string focusClock;
    bool operator==(const MenuEntry&) const = default;
};
struct MenuSetting {
    std::string key="volume", action="number";
    float minimum=0, maximum=10, step=1, initial=10;
    bool operator==(const MenuSetting&) const = default;
};
struct MenuStateBinding {
    std::string key, source="selected", operation="value", factor;
    float compare=0, scale=1, offset=0, minimum=-100000, maximum=100000;
    float factorDefault=1;
    bool operator==(const MenuStateBinding&) const = default;
};
struct MenuConfigTransition {
    bool enabled=false;
    std::string cameraTarget,returnCameraTarget;
    float enterDuration=2,returnDuration=.55f,zoomDelay=.45f;
    bool operator==(const MenuConfigTransition&) const = default;
};
struct MenuConfiguration {
    std::vector<MenuEntry> entries{{0,"loadScene","",0,0},{1,"settings","",1,1},{2,"quit","",2,1}};
    std::vector<MenuSetting> settings{{"volume","number",0,10,1,10},{"motion","toggle",0,1,1,1},{"","save",0,1,1,0}};
    std::array<std::string,6> inputs{"MoveForward","MoveBack","MoveLeft","MoveRight","Confirm","Cancel"};
    std::array<std::string,4> cues{"Select","Confirm","Back","Error"};
    std::vector<MenuStateBinding> bindings;
    bool pressAnyButton=false;
    std::string focusState="cameraFocus";
    MenuConfigTransition configTransition;
    bool settingsScreen=false;
    std::string returnScene;
    bool operator==(const MenuConfiguration&) const = default;
};
inline MenuConfiguration DefaultMenuConfiguration() {
    MenuConfiguration c;
    c.entries[0].focusClock="homeFocusTime";
    c.entries[1].focusClock="configFocusTime";
    c.entries[2].focusClock="quitFocusTime";
    for(const auto* key:{"screen","screenNotSettings","selected","row","gamepad","intro","volume","motion","saveFailed","cameraFocus","focusView","quitTransition","transition"})
        c.bindings.push_back({key,key});
    c.bindings.push_back({"pulse","selectionPulse","value","selectionOffset"});
    c.bindings.back().factorDefault=12;
    c.bindings.push_back({"transitionPink","transition","value","transitionPinkScale",0,1,0,0,1});
    c.bindings.back().factorDefault=1.25f;
    for(int i=0;i<3;++i) {
        c.bindings.push_back({"inactive"+std::to_string(i),"selected","notEqual","",static_cast<float>(i)});
        c.bindings.push_back({"rowInactive"+std::to_string(i),"row","notEqual","",static_cast<float>(i)});
    }
    c.bindings.push_back({"startEmphasis","selected","emphasis","inactiveOpacity",0});
    c.bindings.push_back({"configEmphasis","selected","emphasis","inactiveOpacity",1});
    c.bindings.push_back({"quitEmphasis","selected","emphasis","inactiveOpacity",2});
    for(auto& binding:c.bindings) if(binding.operation=="emphasis") binding.factorDefault=.6f;
    return c;
}
inline float EvaluateMenuBinding(const MenuStateBinding& binding,float value,const std::map<std::string,float>& defaults) {
    const auto found=defaults.find(binding.factor);
    const float factor=binding.factor.empty()?1.0f:found==defaults.end()?binding.factorDefault:found->second;
    if(binding.operation=="equal") value=value==binding.compare?1.0f:0.0f;
    else if(binding.operation=="notEqual") value=value!=binding.compare?1.0f:0.0f;
    else if(binding.operation=="emphasis") value=value==binding.compare?1.0f:factor;
    if(binding.operation!="emphasis") value*=factor;
    return std::clamp(value*binding.scale+binding.offset,binding.minimum,binding.maximum);
}
namespace MenuValidation {
inline void Require(bool ok) {if (!ok) throw std::runtime_error("Invalid menu configuration");}
inline bool Key(const std::string &value) { return Engine::ValidString(value, 128, true) && value.find_first_of("=&") == std::string::npos; }
inline bool Number(float value) { return Engine::InRange(value, -100000, 100000); }
inline void Entry(const MenuEntry &entry, std::set<int> &indices) {
    Require(entry.index >= 0 && entry.index < 128 && indices.insert(entry.index).second && Number(entry.focus) && Number(entry.view) && Engine::ValidString(entry.focusClock, 128));
    Require(Engine::OneOf(entry.action, {"loadScene", "quit", "settings", "setState"}));
    Require(Engine::ValidString(entry.target, 16384));
    if (entry.action == "loadScene" && !entry.target.empty())
        Require(entry.target.starts_with("Assets/Scenes/") && entry.target.ends_with(".json") && entry.target.find("..") == std::string::npos && entry.target.find('\\') == std::string::npos && entry.target.find(':') == std::string::npos);
}
inline void Setting(const MenuSetting &setting, std::set<std::string> &keys) {
    Require(Engine::OneOf(setting.action, {"number", "toggle", "save"}));
    Require(setting.action == "save" || (Key(setting.key) && keys.insert(setting.key).second));
    Require(Number(setting.minimum) && Number(setting.maximum) && Number(setting.step) && Number(setting.initial) && setting.minimum < setting.maximum && setting.step > 0 && Engine::InRange(setting.initial, setting.minimum, setting.maximum));
}
inline void Binding(const MenuStateBinding &binding, std::set<std::string> &keys) {
    Require(Key(binding.key) && keys.insert(binding.key).second && Key(binding.source) && Engine::ValidString(binding.factor, 128));
    Require(Engine::OneOf(binding.operation, {"value", "equal", "notEqual", "emphasis"}));
    for (const auto value : {binding.compare, binding.scale, binding.offset, binding.minimum, binding.maximum, binding.factorDefault}) Require(Number(value));
    Require(binding.minimum <= binding.maximum);
}
}
inline void ValidateMenu(const MenuConfiguration& configuration) {
    using namespace MenuValidation;
    Require(configuration.entries.size()<=128 && configuration.settings.size()<=128 && configuration.bindings.size()<=256);
    std::set<int> indices;
    for(const auto& entry:configuration.entries) Entry(entry,indices);
    std::set<std::string> keys;
    for(const auto& setting:configuration.settings) Setting(setting,keys);
    keys.clear();
    for(const auto& binding:configuration.bindings) Binding(binding,keys);
    for(const auto& input:configuration.inputs) Require(Engine::ValidString(input,128));
    for(const auto& cue:configuration.cues) Require(Engine::ValidString(cue,128));
    Require(Key(configuration.focusState));
    const auto& transition=configuration.configTransition;
    Require(Engine::ValidString(transition.cameraTarget,128) && Engine::ValidString(transition.returnCameraTarget,128));
    Require(!configuration.settingsScreen || (!configuration.returnScene.empty() && configuration.returnScene.starts_with("Assets/Scenes/") && configuration.returnScene.ends_with(".json") && configuration.returnScene.find("..") == std::string::npos && configuration.returnScene.find_first_of("\\:")==std::string::npos));
    Require(!transition.enabled || !transition.cameraTarget.empty());
    Require(Engine::InRange(transition.enterDuration,.1f,10) && Engine::InRange(transition.returnDuration,.1f,10) &&
        Engine::InRange(transition.zoomDelay,0,transition.enterDuration));
}
}
