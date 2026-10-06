#pragma once
#include <SceneRuntime/SceneUi.h>
#include <Engine/Audio/AudioSystem.h>
namespace SceneRuntime {
class SceneAudio final {
public:
    bool Initialize(const std::filesystem::path& root,const SceneLayout& layout,std::string& error);
    void Update(const SceneLayout&,const UiState& state,bool active=true);
    bool Play(const std::string& object);
    bool IsPlaying(const std::string& object) const;
    float Volume(const std::string& object) const;
    void Cue(const std::string& cue);
    void Pause(bool paused);
    void Stop();
private:
    struct Source {Engine::SoundHandle handle=0; bool loop=false,awake=false,started=false;};
    bool paused_=false;
    Engine::AudioSystem audio_;
    std::map<std::string,Source> sources_;
    std::map<std::string,std::string> cues_;
};
}
