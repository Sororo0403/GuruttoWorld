#pragma once
#include <Engine/Audio/AudioSystem.h>
#include <optional>
namespace Editor {
class AudioPreview final {
public:
    struct Request {std::filesystem::path clip; float volume=1; bool loop=false;};
    static inline std::optional<Request> pending;
    static void Play(std::filesystem::path clip,float volume=1,bool loop=false) {pending=Request{std::move(clip),volume,loop};}
    static void StopRequest() {pending=Request{};}
    void Process(const std::filesystem::path& root) {
        if(!pending) return; auto request=std::move(*pending); pending.reset();
        audio_.Shutdown(); error.clear(); if(request.clip.empty()) return;
        if(!audio_.Initialize()) {error="音声出力を利用できません"; return;}
        const auto sound=audio_.Load(root/request.clip);
        if(!sound) {error="音声アセットをデコードできません"; return;}
        audio_.SetVolume(sound,request.volume); audio_.Play(sound,request.loop); error.clear();
    }
    std::string error;
private: Engine::AudioSystem audio_;
};
}
