#include "AudioJson.h"
#include <cmath>
#include <algorithm>
#include <set>
#include <stdexcept>
namespace {
using Engine::Json;
float Number(const Json& o,const char* name,float fallback,float low,float high) {
    if(!o.contains(name)) return fallback;
    const auto number=Engine::JsonNumber(o.at(name));
    if(!std::isfinite(number)||number<low||number>high) throw std::runtime_error("Audio property out of range");
    return static_cast<float>(number);
}
std::string String(const Json& o,const char* name,const char* fallback="") {
    auto value=o.contains(name)?o.at(name).get<std::string>():std::string(fallback);
    if(value.size()>128||value.find('\0')!=std::string::npos) throw std::runtime_error("Audio string too long");
    return value;
}
std::filesystem::path Clip(const Json& o) {
    const auto text=o.at("clip").get<std::string>(); const std::filesystem::path path(std::u8string(text.begin(),text.end()));
    if(text.size()>16384||text.find('\0')!=std::string::npos||(!text.empty()&&!text.starts_with("Assets/"))||path.is_absolute()||path.has_root_name()||
        std::any_of(path.begin(),path.end(),[](const auto& part){return part=="..";})) throw std::runtime_error("Invalid audio clip path");
    return path;
}
Json Base(const std::string& id,bool enabled,const char* type) {return {{"id",id},{"enabled",enabled},{"type",type}};}
void ReadSource(const Json& o,SceneRuntime::ScenePlacement& p) {
    if(p.audioSource) throw std::runtime_error("Duplicate AudioSource");
    SceneRuntime::AudioSourceComponent c;
    c.id=String(o,"id"); c.enabled=o.at("enabled").get<bool>(); c.clip=Clip(o);
    c.volume=Number(o,"volume",1,0,1); c.loop=o.at("loop").get<bool>(); c.playOnAwake=o.at("playOnAwake").get<bool>();
    c.cue=String(o,"cue"); c.volumeBinding=String(o,"volumeBinding"); c.bus=String(o,"bus","Master");
    if(c.bus.empty()) throw std::runtime_error("Audio bus name cannot be empty");
    c.spatial=o.value("spatial",false); c.streaming=o.value("streaming",false);
    c.minimumDistance=Number(o,"minimumDistance",1,.001f,999999); c.maximumDistance=Number(o,"maximumDistance",100,.002f,1000000);
    if(c.maximumDistance<=c.minimumDistance) throw std::runtime_error("Audio distance range reversed");
    c.spatialBlend=Number(o,"spatialBlend",1,0,1); c.pitch=Number(o,"pitch",1,.25f,2); c.lowPass=Number(o,"lowPass",1,0,1);
    p.audioSource=std::move(c);
}
void ReadListener(const Json& o,SceneRuntime::ScenePlacement& p) {
    if(p.audioListener) throw std::runtime_error("Duplicate AudioListener");
    SceneRuntime::AudioListenerComponent c; c.id=String(o,"id"); c.enabled=o.at("enabled").get<bool>();
    c.volume=Number(o,"volume",1,0,1); p.audioListener=std::move(c);
}
void ReadMixer(const Json& o,SceneRuntime::ScenePlacement& p) {
    if(p.audioMixer) throw std::runtime_error("Duplicate AudioMixer");
    SceneRuntime::AudioMixerComponent c; c.id=String(o,"id"); c.enabled=o.at("enabled").get<bool>(); c.groups.clear();
    const auto& groups=Engine::JsonArray(o.at("groups")); if(groups.size()>64) throw std::runtime_error("Too many audio buses");
    std::set<std::string> names;
    for(const auto& entry:groups) {
        SceneRuntime::AudioMixerGroup g; g.name=String(entry,"name"); g.volumeBinding=String(entry,"volumeBinding");
        if(g.name.empty()||!names.insert(g.name).second) throw std::runtime_error("Duplicate or empty audio bus");
        g.volume=Number(entry,"volume",1,0,1); g.lowPass=Number(entry,"lowPass",1,0,1); g.reverb=Number(entry,"reverb",0,0,1); g.mute=entry.value("mute",false);
        c.groups.push_back(std::move(g));
    }
    p.audioMixer=std::move(c);
}
}
namespace SceneRuntime {
bool ReadAudioComponent(const Json& o,ScenePlacement& p,const std::string& type) {
    if(type=="AudioSource") ReadSource(o,p);
    else if(type=="AudioListener") ReadListener(o,p);
    else if(type=="AudioMixer") ReadMixer(o,p);
    else return false;
    return true;
}
void WriteAudioComponents(Json& array,const ScenePlacement& p) {
    if(p.audioSource) {
        const auto& c=*p.audioSource; auto o=Base(c.id,c.enabled,"AudioSource"); const auto text=c.clip.generic_u8string();
        o["clip"]=std::string(text.begin(),text.end()); o["volume"]=c.volume; o["loop"]=c.loop; o["playOnAwake"]=c.playOnAwake;
        o["cue"]=c.cue; o["volumeBinding"]=c.volumeBinding; o["bus"]=c.bus; o["spatial"]=c.spatial; o["streaming"]=c.streaming;
        o["minimumDistance"]=c.minimumDistance; o["maximumDistance"]=c.maximumDistance; o["spatialBlend"]=c.spatialBlend; o["pitch"]=c.pitch; o["lowPass"]=c.lowPass;
        array.push_back(std::move(o));
    }
    if(p.audioListener) {const auto& c=*p.audioListener; auto o=Base(c.id,c.enabled,"AudioListener"); o["volume"]=c.volume; array.push_back(std::move(o));}
    if(p.audioMixer) {
        const auto& c=*p.audioMixer; auto o=Base(c.id,c.enabled,"AudioMixer"); auto groups=Json::array();
        for(const auto& g:c.groups) groups.push_back({{"name",g.name},{"volumeBinding",g.volumeBinding},{"volume",g.volume},{"lowPass",g.lowPass},{"reverb",g.reverb},{"mute",g.mute}});
        o["groups"]=std::move(groups); array.push_back(std::move(o));
    }
}
}
