#pragma once
#include <string>
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
struct MenuConfiguration {
    std::vector<MenuEntry> entries{{0,"loadScene","",0,0},{1,"settings","",1,1},{2,"quit","",2,1}};
    std::vector<MenuSetting> settings{{"volume","number",0,10,1,10},{"motion","toggle",0,1,1,1},{"","save",0,1,1,0}};
    std::array<std::string,6> inputs{"MoveForward","MoveBack","MoveLeft","MoveRight","Confirm","Cancel"};
    std::array<std::string,4> cues{"Select","Confirm","Back","Error"};
    std::vector<MenuStateBinding> bindings;
    bool pressAnyButton=false;
    std::string focusState="cameraFocus";
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
inline void ValidateMenu(const MenuConfiguration& c) {
    const auto validString=[](const std::string& s){return s.size()<=128 && s.find('\0')==std::string::npos;};
    const auto key=[&](const std::string& s){return !s.empty() && validString(s) && s.find_first_of("=&")==std::string::npos;};
    const auto number=[](float f){return std::isfinite(f) && std::abs(f)<=100000;};
    const auto require=[](bool ok){if(!ok) throw std::runtime_error("Invalid menu configuration");};
    require(c.entries.size()<=128 && c.settings.size()<=128 && c.bindings.size()<=256);
    std::set<int> indices;
    for(const auto& e:c.entries) {
        require(e.index>=0 && e.index<128 && indices.insert(e.index).second && number(e.focus) && number(e.view) && validString(e.focusClock));
        require(e.action=="loadScene" || e.action=="quit" || e.action=="settings" || e.action=="setState");
        require(e.target.size()<=16384 && e.target.find('\0')==std::string::npos);
        if(e.action=="loadScene" && !e.target.empty()) require(e.target.starts_with("Assets/Scenes/") && e.target.ends_with(".json") && e.target.find("..") == std::string::npos && e.target.find('\\')==std::string::npos && e.target.find(':')==std::string::npos);
    }
    std::set<std::string> keys;
    for(const auto& s:c.settings) {
        require(s.action=="number" || s.action=="toggle" || s.action=="save");
        require(s.action=="save" || (key(s.key) && keys.insert(s.key).second));
        require(number(s.minimum) && number(s.maximum) && number(s.step) && number(s.initial) && s.minimum<s.maximum && s.step>0 && s.initial>=s.minimum && s.initial<=s.maximum);
    }
    keys.clear();
    for(const auto& b:c.bindings) {
        require(key(b.key) && keys.insert(b.key).second && key(b.source) && validString(b.factor));
        require(b.operation=="value" || b.operation=="equal" || b.operation=="notEqual" || b.operation=="emphasis");
        require(number(b.compare) && number(b.scale) && number(b.offset) && number(b.minimum) && number(b.maximum) && number(b.factorDefault) && b.minimum<=b.maximum);
    }
    for(const auto& s:c.inputs) require(validString(s));
    for(const auto& s:c.cues) require(validString(s));
    require(key(c.focusState));
}
}
