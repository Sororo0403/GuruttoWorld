#include <SceneRuntime/SceneAudio.h>
#include <algorithm>
namespace SceneRuntime {
bool SceneAudio::Initialize(const std::filesystem::path& root,const SceneLayout& layout,std::string& error) {
    Stop(); audio_.Shutdown(); sources_.clear(); cues_.clear(); paused_=false;
    bool needed=false; for(const auto& p:layout.objects) needed|=p.audioSource && p.audioSource->enabled && !p.audioSource->clip.empty();
    if(!needed) {error.clear(); return true;}
    if(!audio_.Initialize()) {error="Audio output unavailable"; return false;}
    for(const auto& p:layout.objects) {
        if(!p.audioSource || !p.audioSource->enabled || p.audioSource->clip.empty()) continue;
        const auto& c=*p.audioSource; const auto h=audio_.Load(root/c.clip);
        if(!h) {error="Cannot load audio: "+p.id; Stop(); audio_.Shutdown(); sources_.clear(); return false;}
        audio_.SetVolume(h,c.volume); sources_[p.id]={h,c.loop,c.playOnAwake,false};
        if(!c.cue.empty()) cues_.try_emplace(c.cue,p.id);
    }
    error.clear(); return true;
}
void SceneAudio::Update(const SceneLayout& layout,const UiState& state,bool active) {
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
