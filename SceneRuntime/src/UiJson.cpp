#include "UiJson.h"
#include <SceneRuntime/SceneUi.h>
#include <cmath>
#include <algorithm>
#include <stdexcept>
namespace SceneRuntime {
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(MenuEntry,index,action,target,focus,view,focusClock)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(MenuSetting,key,action,minimum,maximum,step,initial)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(MenuStateBinding,key,source,operation,factor,compare,scale,offset,minimum,maximum,factorDefault)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(MenuConfiguration,entries,settings,inputs,cues,bindings,pressAnyButton,focusState)
}
namespace {
using Engine::Json;
    using Engine::JsonNumber;
    using Engine::JsonArray;
    using Engine::JsonObject;
float Number(const Json& o,const char* key,float low,float high) {
    const auto v=JsonNumber(o.at(key));
    if (!std::isfinite(v) || v<low || v>high) throw std::runtime_error("UI property out of range");
    return static_cast<float>(v);
}
template<size_t N> std::array<float,N> Vector(const Json& o,const char* key,float low,float high) {
    const auto& a=JsonArray(o.at(key)); if(a.size()!=N) throw std::runtime_error("UI vector size");
    std::array<float,N> r{};
    for(uint32_t i=0;i<N;++i) {const auto v=JsonNumber(a.at(i)); if(!std::isfinite(v)||v<low||v>high) throw std::runtime_error("UI vector range"); r[i]=static_cast<float>(v);}
    return r;
}
std::string String(const Json& o,const char* key) {
    auto s=o.at(key).get<std::string>();
    if(s.size()>16384 || s.find('\0')!=std::string::npos) throw std::runtime_error("Invalid UI string"); return s;
}
std::filesystem::path Path(const Json& o,const char* key) {
    auto s=String(o,key); std::filesystem::path p(std::u8string(s.begin(),s.end()));
    if(!s.empty() && (!s.starts_with("Assets/") || p.is_absolute() || p.has_root_name())) throw std::runtime_error("Asset must be relative to Assets");
    if(std::any_of(p.begin(),p.end(),[](const auto& part){return part=="..";})) throw std::runtime_error("Asset traversal"); return p;
}
void Put(Json& o,const char* k,float v){o[k]=v;}
void Put(Json& o,const char* k,bool v){o[k]=v;}
void Put(Json& o,const char* k,const std::string& v){o[k]=v;}
void Put(Json& o,const char* k,const std::filesystem::path& v){auto u=v.generic_u8string(); Put(o,k,std::string(u.begin(),u.end()));}
template<size_t N> void Put(Json& o,const char* k,const std::array<float,N>& v){o[k]=v;}
void ReadCanvas(const Json& o,SceneRuntime::ScenePlacement& p) {
    if(p.canvas) throw std::runtime_error("Duplicate Canvas");
    SceneRuntime::CanvasComponent c; c.id=String(o,"id"); c.enabled=o.at("enabled").get<bool>();
    c.referenceSize=Vector<2>(o,"referenceSize",1.0f,8192.0f);
    if(o.contains("scaleWithScreen")) c.scaleWithScreen=o.at("scaleWithScreen").get<bool>();
    if(o.contains("stateDefaults")) for(const auto& [key,value]:JsonObject(o.at("stateDefaults")).items()) {
        static_cast<void>(value);
        if(key.empty() || key.find_first_of("=&")!=std::string::npos || key.find('\0')!=std::string::npos)
            throw std::runtime_error("Invalid Canvas state key");
        c.stateDefaults[key]=Number(o.at("stateDefaults"),key.c_str(),-100000,100000);
    }
    if(o.contains("menu")) {
        for(const auto& entry:JsonArray(o.at("menu").at("entries"))) {
            const auto& index=entry.at("index");
            if(!index.is_number_integer() || JsonNumber(index)<0 || JsonNumber(index)>=128) throw std::runtime_error("Invalid menu index");
        }
        c.menu=o.at("menu").get<SceneRuntime::MenuConfiguration>(); SceneRuntime::ValidateMenu(*c.menu);
        for(const auto& entry:c.menu->entries) if(entry.action=="setState") {
            SceneRuntime::UiState state; if(!state.Assign(entry.target)) throw std::runtime_error("Invalid menu state assignment");
        }
    }
    p.canvas=std::move(c);
}
void WriteCanvas(Json& a,const SceneRuntime::CanvasComponent& c) {
    Json o; Put(o,"id",c.id); Put(o,"type",std::string("Canvas")); Put(o,"enabled",c.enabled);
    Put(o,"referenceSize",c.referenceSize); Put(o,"scaleWithScreen",c.scaleWithScreen);
    Json defaults=Json::object(); for(const auto& [key,value]:c.stateDefaults) Put(defaults,key.c_str(),value); o["stateDefaults"]=defaults;
    if(c.menu) { SceneRuntime::ValidateMenu(*c.menu); o["menu"]=*c.menu; }
    a.push_back(o);
}
void ReadRectTransform(const Json& o,SceneRuntime::ScenePlacement& p) {
    if(p.rectTransform) throw std::runtime_error("Duplicate RectTransform");
    SceneRuntime::RectTransformComponent c; c.id=String(o,"id"); c.enabled=o.at("enabled").get<bool>();
    c.anchorMin=Vector<2>(o,"anchorMin",0.0f,1.0f);
    c.anchorMax=Vector<2>(o,"anchorMax",0.0f,1.0f);
    c.pivot=Vector<2>(o,"pivot",0.0f,1.0f);
    c.position=Vector<2>(o,"position",-100000.0f,100000.0f);
    c.size=Vector<2>(o,"size",0.0f,100000.0f);
    c.rotation=Number(o,"rotation",-100000.0f,100000.0f);
    c.introDelay=Number(o,"introDelay",0.0f,0.99f);
    c.introOffset=Number(o,"introOffset",-100000.0f,100000.0f);
    c.visibleWhen=String(o,"visibleWhen");
    c.offsetBinding=String(o,"offsetBinding");
    c.opacityBinding=String(o,"opacityBinding");
    if(o.contains("widthBinding")) c.widthBinding=String(o,"widthBinding");
    for(size_t i=0;i<2;++i) if(c.anchorMin[i]>c.anchorMax[i]) throw std::runtime_error("Reversed anchors");
    SceneRuntime::UiState check; if(!check.Assign(c.visibleWhen)) throw std::runtime_error("Invalid visibility expression");
    p.rectTransform=std::move(c);
}
void WriteRectTransform(Json& a,const SceneRuntime::RectTransformComponent& c) {
    Json o; Put(o,"id",c.id); Put(o,"type",std::string("RectTransform")); Put(o,"enabled",c.enabled);
    Put(o,"anchorMin",c.anchorMin);
    Put(o,"anchorMax",c.anchorMax);
    Put(o,"pivot",c.pivot);
    Put(o,"position",c.position);
    Put(o,"size",c.size);
    Put(o,"rotation",c.rotation);
    Put(o,"introDelay",c.introDelay);
    Put(o,"introOffset",c.introOffset);
    Put(o,"visibleWhen",c.visibleWhen);
    Put(o,"offsetBinding",c.offsetBinding);
    Put(o,"opacityBinding",c.opacityBinding);
    Put(o,"widthBinding",c.widthBinding);
    a.push_back(o);
}
void ReadImage(const Json& o,SceneRuntime::ScenePlacement& p) {
    if(p.image) throw std::runtime_error("Duplicate Image");
    SceneRuntime::ImageComponent c; c.id=String(o,"id"); c.enabled=o.at("enabled").get<bool>();
    c.texture=Path(o,"texture");
    c.uv=Vector<4>(o,"uv",0.0f,1.0f);
    c.color=Vector<4>(o,"color",0.0f,1.0f);
    if(c.uv[0]>c.uv[2] || c.uv[1]>c.uv[3]) throw std::runtime_error("Reversed UV");
    p.image=std::move(c);
}
void WriteImage(Json& a,const SceneRuntime::ImageComponent& c) {
    Json o; Put(o,"id",c.id); Put(o,"type",std::string("Image")); Put(o,"enabled",c.enabled);
    Put(o,"texture",c.texture);
    Put(o,"uv",c.uv);
    Put(o,"color",c.color);
    a.push_back(o);
}
void ReadText(const Json& o,SceneRuntime::ScenePlacement& p) {
    if(p.text) throw std::runtime_error("Duplicate Text");
    SceneRuntime::TextComponent c; c.id=String(o,"id"); c.enabled=o.at("enabled").get<bool>();
    c.text=String(o,"text");
    c.font=String(o,"font");
    c.fontSize=Number(o,"fontSize",1.0f,512.0f);
    c.color=Vector<4>(o,"color",0.0f,1.0f);
    p.text=std::move(c);
}
void WriteText(Json& a,const SceneRuntime::TextComponent& c) {
    Json o; Put(o,"id",c.id); Put(o,"type",std::string("Text")); Put(o,"enabled",c.enabled);
    Put(o,"text",c.text);
    Put(o,"font",c.font);
    Put(o,"fontSize",c.fontSize);
    Put(o,"color",c.color);
    a.push_back(o);
}
void ReadButton(const Json& o,SceneRuntime::ScenePlacement& p) {
    if(p.button) throw std::runtime_error("Duplicate Button");
    SceneRuntime::ButtonComponent c; c.id=String(o,"id"); c.enabled=o.at("enabled").get<bool>();
    c.action=String(o,"action");
    c.target=String(o,"target");
    if(o.contains("event")) c.event=String(o,"event");
    if(o.contains("shortcut")) c.shortcut=String(o,"shortcut");
    if(o.contains("inputAction")) c.inputAction=String(o,"inputAction");
    if(c.inputAction.empty()) {
        if(c.shortcut=="space") c.inputAction="Jump";
        else if(c.shortcut=="escape") c.inputAction="Cancel";
        else if(c.shortcut=="1") c.inputAction="SelectModel1";
        else if(c.shortcut=="2") c.inputAction="SelectModel2";
    }
    if(c.inputAction.size()>128 || c.inputAction.find('\0')!=std::string::npos) throw std::runtime_error("Invalid button input action");
    const std::array<std::string_view,5> shortcuts{"","space","escape","1","2"};
    if(std::find(shortcuts.begin(),shortcuts.end(),c.shortcut)==shortcuts.end()) throw std::runtime_error("Unsupported button shortcut");
    c.sound=String(o,"sound");
    c.hoverColor=Vector<4>(o,"hoverColor",0.0f,1.0f);
    c.pressedColor=Vector<4>(o,"pressedColor",0.0f,1.0f);
    const std::array<std::string_view,8> actions{"click","show","hide","toggle","playAudio","loadScene","quit","setState"};
    if(std::find(actions.begin(),actions.end(),c.action)==actions.end()) throw std::runtime_error("Unsupported button action");
    if(c.action=="loadScene" && (!c.target.starts_with("Assets/Scenes/") || std::filesystem::path(c.target).extension()!=".json" || std::filesystem::path(c.target).has_root_name() || c.target.find("..")!=std::string::npos)) throw std::runtime_error("Scene target must be relative to Assets/Scenes");
    SceneRuntime::UiState check; if(c.action=="setState" && !check.Assign(c.target)) throw std::runtime_error("Invalid state assignment");
    p.button=std::move(c);
}
void WriteButton(Json& a,const SceneRuntime::ButtonComponent& c) {
    Json o; Put(o,"id",c.id); Put(o,"type",std::string("Button")); Put(o,"enabled",c.enabled);
    Put(o,"action",c.action);
    Put(o,"target",c.target);
    Put(o,"event",c.event);
    Put(o,"shortcut",c.shortcut);
    Put(o,"inputAction",c.inputAction);
    Put(o,"sound",c.sound);
    Put(o,"hoverColor",c.hoverColor);
    Put(o,"pressedColor",c.pressedColor);
    a.push_back(o);
}
void ReadAudioSource(const Json& o,SceneRuntime::ScenePlacement& p) {
    if(p.audioSource) throw std::runtime_error("Duplicate AudioSource");
    SceneRuntime::AudioSourceComponent c; c.id=String(o,"id"); c.enabled=o.at("enabled").get<bool>();
    c.clip=Path(o,"clip");
    c.volume=Number(o,"volume",0.0f,1.0f);
    c.loop=o.at("loop").get<bool>();
    c.playOnAwake=o.at("playOnAwake").get<bool>();
    c.cue=String(o,"cue");
    c.volumeBinding=String(o,"volumeBinding");
    p.audioSource=std::move(c);
}
void WriteAudioSource(Json& a,const SceneRuntime::AudioSourceComponent& c) {
    Json o; Put(o,"id",c.id); Put(o,"type",std::string("AudioSource")); Put(o,"enabled",c.enabled);
    Put(o,"clip",c.clip);
    Put(o,"volume",c.volume);
    Put(o,"loop",c.loop);
    Put(o,"playOnAwake",c.playOnAwake);
    Put(o,"cue",c.cue);
    Put(o,"volumeBinding",c.volumeBinding);
    a.push_back(o);
}
}
namespace SceneRuntime {
bool ReadUiComponent(const Json& o,ScenePlacement& p,const std::string& type) {
    if(type=="Canvas") ReadCanvas(o,p);
    else if(type=="RectTransform") ReadRectTransform(o,p);
    else if(type=="Image") ReadImage(o,p);
    else if(type=="Text") ReadText(o,p);
    else if(type=="Button") ReadButton(o,p);
    else if(type=="AudioSource") ReadAudioSource(o,p);
    else return false; return true;
}
void WriteUiComponents(Json& a,const ScenePlacement& p) {
    if(p.canvas) WriteCanvas(a,*p.canvas);
    if(p.rectTransform) WriteRectTransform(a,*p.rectTransform);
    if(p.image) WriteImage(a,*p.image);
    if(p.text) WriteText(a,*p.text);
    if(p.button) WriteButton(a,*p.button);
    if(p.audioSource) WriteAudioSource(a,*p.audioSource);
}
}
