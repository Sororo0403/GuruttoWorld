#include "UiJson.h"
#include <winrt/Windows.Foundation.Collections.h>
#include <cmath>
#include <stdexcept>
#undef GetObject
namespace {
using namespace winrt::Windows::Data::Json;
float Number(const JsonObject& o,const wchar_t* key,float low,float high) {
    const auto v=o.GetNamedNumber(key);
    if (!std::isfinite(v) || v<low || v>high) throw std::runtime_error("UI property out of range");
    return static_cast<float>(v);
}
template<size_t N> std::array<float,N> Vector(const JsonObject& o,const wchar_t* key,float low,float high) {
    const auto a=o.GetNamedArray(key); if(a.Size()!=N) throw std::runtime_error("UI vector size");
    std::array<float,N> r{};
    for(uint32_t i=0;i<N;++i) {const auto v=a.GetNumberAt(i); if(!std::isfinite(v)||v<low||v>high) throw std::runtime_error("UI vector range"); r[i]=static_cast<float>(v);}
    return r;
}
std::string String(const JsonObject& o,const wchar_t* key) {
    auto s=winrt::to_string(o.GetNamedString(key));
    if(s.size()>16384 || s.find('\0')!=std::string::npos) throw std::runtime_error("Invalid UI string"); return s;
}
std::filesystem::path Path(const JsonObject& o,const wchar_t* key) {
    auto s=String(o,key); std::filesystem::path p(winrt::to_hstring(s).c_str());
    if(!s.empty() && (!s.starts_with("Assets/") || p.is_absolute() || p.has_root_name())) throw std::runtime_error("Asset must be relative to Assets");
    for(const auto& part:p) if(part==L"..") throw std::runtime_error("Asset traversal"); return p;
}
void Put(JsonObject& o,const wchar_t* k,float v){o.SetNamedValue(k,JsonValue::CreateNumberValue(v));}
void Put(JsonObject& o,const wchar_t* k,bool v){o.SetNamedValue(k,JsonValue::CreateBooleanValue(v));}
void Put(JsonObject& o,const wchar_t* k,const std::string& v){o.SetNamedValue(k,JsonValue::CreateStringValue(winrt::to_hstring(v)));}
void Put(JsonObject& o,const wchar_t* k,const std::filesystem::path& v){auto u=v.generic_u8string(); Put(o,k,std::string(u.begin(),u.end()));}
template<size_t N> void Put(JsonObject& o,const wchar_t* k,const std::array<float,N>& v){JsonArray a; for(auto x:v)a.Append(JsonValue::CreateNumberValue(x)); o.SetNamedValue(k,a);}
void ReadCanvas(const JsonObject& o,SceneRuntime::ScenePlacement& p) {
    if(p.canvas) throw std::runtime_error("Duplicate Canvas");
    SceneRuntime::CanvasComponent c; c.id=String(o,L"id"); c.enabled=o.GetNamedBoolean(L"enabled");
    c.referenceSize=Vector<2>(o,L"referenceSize",1.0f,8192.0f);
    p.canvas=std::move(c);
}
void WriteCanvas(JsonArray& a,const SceneRuntime::CanvasComponent& c) {
    JsonObject o; Put(o,L"id",c.id); Put(o,L"type",std::string("Canvas")); Put(o,L"enabled",c.enabled);
    Put(o,L"referenceSize",c.referenceSize);
    a.Append(o);
}
void ReadRectTransform(const JsonObject& o,SceneRuntime::ScenePlacement& p) {
    if(p.rectTransform) throw std::runtime_error("Duplicate RectTransform");
    SceneRuntime::RectTransformComponent c; c.id=String(o,L"id"); c.enabled=o.GetNamedBoolean(L"enabled");
    c.anchorMin=Vector<2>(o,L"anchorMin",0.0f,1.0f);
    c.anchorMax=Vector<2>(o,L"anchorMax",0.0f,1.0f);
    c.pivot=Vector<2>(o,L"pivot",0.0f,1.0f);
    c.position=Vector<2>(o,L"position",-100000.0f,100000.0f);
    c.size=Vector<2>(o,L"size",0.0f,100000.0f);
    c.rotation=Number(o,L"rotation",-100000.0f,100000.0f);
    c.introDelay=Number(o,L"introDelay",0.0f,0.99f);
    c.introOffset=Number(o,L"introOffset",-100000.0f,100000.0f);
    c.visibleWhen=String(o,L"visibleWhen");
    c.offsetBinding=String(o,L"offsetBinding");
    c.opacityBinding=String(o,L"opacityBinding");
    for(size_t i=0;i<2;++i) if(c.anchorMin[i]>c.anchorMax[i]) throw std::runtime_error("Reversed anchors");
    p.rectTransform=std::move(c);
}
void WriteRectTransform(JsonArray& a,const SceneRuntime::RectTransformComponent& c) {
    JsonObject o; Put(o,L"id",c.id); Put(o,L"type",std::string("RectTransform")); Put(o,L"enabled",c.enabled);
    Put(o,L"anchorMin",c.anchorMin);
    Put(o,L"anchorMax",c.anchorMax);
    Put(o,L"pivot",c.pivot);
    Put(o,L"position",c.position);
    Put(o,L"size",c.size);
    Put(o,L"rotation",c.rotation);
    Put(o,L"introDelay",c.introDelay);
    Put(o,L"introOffset",c.introOffset);
    Put(o,L"visibleWhen",c.visibleWhen);
    Put(o,L"offsetBinding",c.offsetBinding);
    Put(o,L"opacityBinding",c.opacityBinding);
    a.Append(o);
}
void ReadImage(const JsonObject& o,SceneRuntime::ScenePlacement& p) {
    if(p.image) throw std::runtime_error("Duplicate Image");
    SceneRuntime::ImageComponent c; c.id=String(o,L"id"); c.enabled=o.GetNamedBoolean(L"enabled");
    c.texture=Path(o,L"texture");
    c.uv=Vector<4>(o,L"uv",0.0f,1.0f);
    c.color=Vector<4>(o,L"color",0.0f,1.0f);
    if(c.uv[0]>c.uv[2] || c.uv[1]>c.uv[3]) throw std::runtime_error("Reversed UV");
    p.image=std::move(c);
}
void WriteImage(JsonArray& a,const SceneRuntime::ImageComponent& c) {
    JsonObject o; Put(o,L"id",c.id); Put(o,L"type",std::string("Image")); Put(o,L"enabled",c.enabled);
    Put(o,L"texture",c.texture);
    Put(o,L"uv",c.uv);
    Put(o,L"color",c.color);
    a.Append(o);
}
void ReadText(const JsonObject& o,SceneRuntime::ScenePlacement& p) {
    if(p.text) throw std::runtime_error("Duplicate Text");
    SceneRuntime::TextComponent c; c.id=String(o,L"id"); c.enabled=o.GetNamedBoolean(L"enabled");
    c.text=String(o,L"text");
    c.font=String(o,L"font");
    c.fontSize=Number(o,L"fontSize",1.0f,512.0f);
    c.color=Vector<4>(o,L"color",0.0f,1.0f);
    p.text=std::move(c);
}
void WriteText(JsonArray& a,const SceneRuntime::TextComponent& c) {
    JsonObject o; Put(o,L"id",c.id); Put(o,L"type",std::string("Text")); Put(o,L"enabled",c.enabled);
    Put(o,L"text",c.text);
    Put(o,L"font",c.font);
    Put(o,L"fontSize",c.fontSize);
    Put(o,L"color",c.color);
    a.Append(o);
}
void ReadButton(const JsonObject& o,SceneRuntime::ScenePlacement& p) {
    if(p.button) throw std::runtime_error("Duplicate Button");
    SceneRuntime::ButtonComponent c; c.id=String(o,L"id"); c.enabled=o.GetNamedBoolean(L"enabled");
    c.action=String(o,L"action");
    c.target=String(o,L"target");
    c.sound=String(o,L"sound");
    c.hoverColor=Vector<4>(o,L"hoverColor",0.0f,1.0f);
    c.pressedColor=Vector<4>(o,L"pressedColor",0.0f,1.0f);
    if(c.action!="click" && c.action!="show" && c.action!="hide" && c.action!="toggle" && c.action!="playAudio" && c.action!="loadScene" && c.action!="quit") throw std::runtime_error("Unsupported button action");
    p.button=std::move(c);
}
void WriteButton(JsonArray& a,const SceneRuntime::ButtonComponent& c) {
    JsonObject o; Put(o,L"id",c.id); Put(o,L"type",std::string("Button")); Put(o,L"enabled",c.enabled);
    Put(o,L"action",c.action);
    Put(o,L"target",c.target);
    Put(o,L"sound",c.sound);
    Put(o,L"hoverColor",c.hoverColor);
    Put(o,L"pressedColor",c.pressedColor);
    a.Append(o);
}
void ReadAudioSource(const JsonObject& o,SceneRuntime::ScenePlacement& p) {
    if(p.audioSource) throw std::runtime_error("Duplicate AudioSource");
    SceneRuntime::AudioSourceComponent c; c.id=String(o,L"id"); c.enabled=o.GetNamedBoolean(L"enabled");
    c.clip=Path(o,L"clip");
    c.volume=Number(o,L"volume",0.0f,1.0f);
    c.loop=o.GetNamedBoolean(L"loop");
    c.playOnAwake=o.GetNamedBoolean(L"playOnAwake");
    c.cue=String(o,L"cue");
    c.volumeBinding=String(o,L"volumeBinding");
    p.audioSource=std::move(c);
}
void WriteAudioSource(JsonArray& a,const SceneRuntime::AudioSourceComponent& c) {
    JsonObject o; Put(o,L"id",c.id); Put(o,L"type",std::string("AudioSource")); Put(o,L"enabled",c.enabled);
    Put(o,L"clip",c.clip);
    Put(o,L"volume",c.volume);
    Put(o,L"loop",c.loop);
    Put(o,L"playOnAwake",c.playOnAwake);
    Put(o,L"cue",c.cue);
    Put(o,L"volumeBinding",c.volumeBinding);
    a.Append(o);
}
}
namespace SceneRuntime {
bool ReadUiComponent(const JsonObject& o,ScenePlacement& p,const std::string& type) {
    if(type=="Canvas") ReadCanvas(o,p);
    else if(type=="RectTransform") ReadRectTransform(o,p);
    else if(type=="Image") ReadImage(o,p);
    else if(type=="Text") ReadText(o,p);
    else if(type=="Button") ReadButton(o,p);
    else if(type=="AudioSource") ReadAudioSource(o,p);
    else return false; return true;
}
void WriteUiComponents(JsonArray& a,const ScenePlacement& p) {
    if(p.canvas) WriteCanvas(a,*p.canvas);
    if(p.rectTransform) WriteRectTransform(a,*p.rectTransform);
    if(p.image) WriteImage(a,*p.image);
    if(p.text) WriteText(a,*p.text);
    if(p.button) WriteButton(a,*p.button);
    if(p.audioSource) WriteAudioSource(a,*p.audioSource);
}
}
