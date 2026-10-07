#include <SceneRuntime/SceneAudio.h>
#include <algorithm>
#include <Engine/Core/Log.h>
namespace SceneRuntime {
bool SceneAudio::Initialize(const std::filesystem::path& root,const SceneLayout& layout,std::string& error) {
    Stop(); audio_.Shutdown(); sources_.clear(); cues_.clear(); paused_=false; initialized_=false; root_=root;
    return Refresh(layout,error);
}
bool SceneAudio::Refresh(const SceneLayout& layout,std::string& error) {
    bool needed=false; for(const auto& p:layout.objects) needed|=p.audioSource && p.audioSource->enabled && !p.audioSource->clip.empty();
    if(needed && !initialized_) {
        if(!audio_.Initialize()) {error="Audio output unavailable"; return false;}
        initialized_=true;
    }
    std::map<std::string,Source> pending;
    std::map<std::string,std::string> cues;
    std::vector<Engine::SoundHandle> loaded;
    for(const auto& p:layout.objects) {
        if(!p.audioSource || !p.audioSource->enabled || p.audioSource->clip.empty()) continue;
        const auto& c=*p.audioSource;
        const auto old=sources_.find(p.id);
        if(old!=sources_.end() && old->second.clip==c.clip && old->second.loop==c.loop) pending[p.id]=old->second;
        else {
            const auto h=audio_.Load(root_/c.clip);
            if(!h) {for(const auto handle:loaded) audio_.Unload(handle); error="Cannot load audio: "+p.id; return false;}
            loaded.push_back(h); audio_.SetVolume(h,c.volume);
            pending[p.id]={h,c.loop,c.playOnAwake,false,c.clip};
        }
        pending[p.id].awake=c.playOnAwake;
        if(!c.cue.empty()) cues.try_emplace(c.cue,p.id);
    }
    for(const auto& [id,s]:sources_) {
        const auto replacement=pending.find(id);
        if(replacement==pending.end() || replacement->second.handle!=s.handle) audio_.Unload(s.handle);
    }
    sources_=std::move(pending); cues_=std::move(cues);
    error.clear(); return true;
}
void SceneAudio::Update(const SceneLayout& layout,const UiState& state,bool active) {
    std::string error;
    if(!Refresh(layout,error)) Engine::Log::Warning(error);
    Pause(!active); if(!active) return;
    for(const auto& p:layout.objects) {
        const auto i=sources_.find(p.id); if(i==sources_.end() || !p.audioSource) continue;
        const auto& c=*p.audioSource; auto& s=i->second;
        audio_.SetVolume(s.handle,std::clamp(c.volume*state.Value(c.volumeBinding,1),0.0f,1.0f));
        if(c.enabled && s.awake && !s.started) {s.started=audio_.Play(s.handle,s.loop);}
        if(!c.enabled) audio_.Stop(s.handle);
    }
}
bool SceneAudio::Play(const std::string& object) {
    const auto i=sources_.find(object);
    if(i==sources_.end() || !audio_.Play(i->second.handle,i->second.loop)) return false;
    if(paused_) audio_.Pause(i->second.handle,true); return true;
}
bool SceneAudio::IsPlaying(const std::string& object) const {const auto i=sources_.find(object); return i!=sources_.end() && audio_.IsPlaying(i->second.handle);}
float SceneAudio::Volume(const std::string& object) const {const auto i=sources_.find(object); return i==sources_.end()?0:audio_.GetVolume(i->second.handle);}
void SceneAudio::Cue(const std::string& cue) { const auto i=cues_.find(cue); if(i!=cues_.end()) Play(i->second); }
void SceneAudio::Pause(bool paused) {if(paused_==paused) return; paused_=paused; for(const auto& [id,s]:sources_) {static_cast<void>(id); audio_.Pause(s.handle,paused);}}
void SceneAudio::Stop() {for(const auto& [id,s]:sources_) {static_cast<void>(id); audio_.Stop(s.handle);}}
}
