#pragma once
#include <SceneRuntime/SceneAudio.h>
#include <Engine/Core/Json.h>
#include <fstream>
#include <thread>
#include <chrono>
#include <cmath>
namespace AudioFeatureValidation {
/// <summary>音響のテスト条件を確認します。</summary>
inline void Require(bool condition,const char* message) {if(!condition) throw std::runtime_error(message);}
/// <summary>全展開との差を検証できる長いPCMクリップを作成します。</summary>
inline std::filesystem::path Fixture() {
    const std::filesystem::path root="generated/tests/audio-features";
    std::filesystem::create_directories(root/"Assets/Audio"); std::ofstream file(root/"Assets/Audio/long.wav",std::ios::binary);
    constexpr DWORD bytes=48000*2*30; const auto put=[&](auto value){file.write(reinterpret_cast<const char*>(&value),sizeof(value));};
    file.write("RIFF",4); put(DWORD(36+bytes)); file.write("WAVEfmt ",8); put(DWORD(16)); put(WORD(WAVE_FORMAT_PCM)); put(WORD(1));
    put(DWORD(48000)); put(DWORD(96000)); put(WORD(2)); put(WORD(16)); file.write("data",4); put(bytes);
    std::vector<char> silence(bytes,0); file.write(silence.data(),silence.size()); file.close(); Require(bool(file),"audio streaming fixture write"); return root;
}
/// <summary>保存互換、重複・範囲検証、複製用コピーを確認します。</summary>
inline void Schema() {
    SceneRuntime::ScenePlacement source; source.id="source"; source.audioSource.emplace(); source.audioSource->clip="Assets/Audio/test.wav";
    source.audioSource->spatial=true; source.audioSource->streaming=true; source.audioSource->bus="Music"; source.audioSource->pitch=.8f;
    SceneRuntime::ScenePlacement listener; listener.id="listener"; listener.audioListener.emplace(); listener.audioListener->volume=.6f;
    SceneRuntime::ScenePlacement mixer; mixer.id="mixer"; mixer.audioMixer.emplace(); mixer.audioMixer->groups.push_back({"Music","musicVolume",.4f,.7f,.35f,false});
    SceneRuntime::SceneLayout layout; layout.objects={source,listener,mixer};
    const auto restored=SceneRuntime::SceneLayout::Parse(layout.Serialize());
    for(size_t i=0;i<layout.objects.size();++i) Require(layout.objects[i].SameComponents(restored.objects[i]),"3D audio mixer listener roundtrip");
    SceneRuntime::ScenePlacement copy; copy.CopyComponents(mixer); Require(copy.audioMixer==mixer.audioMixer&&copy.HasComponentId("mixer"),"audio mixer copy and ID inventory");
    copy.CopyComponents(listener); Require(copy.audioListener==listener.audioListener&&copy.HasComponentId("listener"),"audio listener copy and ID inventory");
    auto json=Engine::Json::parse(layout.Serialize());
    auto& components=json["objects"][0]["components"];
    for(auto& c:components) if(c["type"]=="AudioSource") for(const char* field:{"spatial","streaming","bus","pitch","lowPass","spatialBlend","minimumDistance","maximumDistance"}) c.erase(field);
    const auto old=SceneRuntime::SceneLayout::Parse(json.dump()); Require(!old.objects[0].audioSource->spatial&&!old.objects[0].audioSource->streaming&&old.objects[0].audioSource->bus=="Master","old audio scenes retain 2D cached playback");
    layout.objects[0].audioSource->maximumDistance=.1f; bool rejected=false;
    try {static_cast<void>(layout.Serialize());} catch(const std::exception&) {rejected=true;} Require(rejected,"invalid audio distance cannot save");
    layout.objects[0]=source; layout.objects[2].audioMixer->groups.push_back(layout.objects[2].audioMixer->groups.front()); rejected=false;
    try {static_cast<void>(layout.Serialize());} catch(const std::exception&) {rejected=true;} Require(rejected,"duplicate mixer groups cannot save");
}
/// <summary>実XAudio2ボイスの3D行列・ルート・エフェクト・ストリームを確認します。</summary>
inline void Playback() {
    const auto root=Fixture(); Engine::AudioSystem audio; Require(audio.Initialize(),"spatial mixer output initialize");
    const auto sound=audio.Load(root/"Assets/Audio/long.wav",true); Require(sound!=0,"streaming open");
    Require(audio.SetBus("Music",{.5f,.6f,.3f,false})&&audio.Route(sound,"Music"),"real submix reverb filter routing");
    Require(!audio.SetBus("bad",{-1,1,0,false})&&!audio.Route(sound,"missing"),"invalid mixer settings preserve routing");
    Engine::AudioSpatialSettings spatial; spatial.enabled=true; spatial.position={-5,0,5}; spatial.maximumDistance=20;
    Require(audio.SetSpatial(sound,spatial)&&audio.SetVolume(sound,0)&&audio.Play(sound,true),"spatial stream plays silently");
    const auto left=audio.OutputMatrix(sound); Require(left.size()==2&&left[0]>left[1],"left emitter pans left");
    spatial.position={5,0,5}; Require(audio.SetSpatial(sound,spatial),"moving emitter"); const auto right=audio.OutputMatrix(sound);
    Require(right[1]>right[0],"right emitter pans right");
    spatial.position={0,0,25}; Require(audio.SetSpatial(sound,spatial),"distant emitter"); const auto distant=audio.OutputMatrix(sound);
    Require(std::abs(distant[0])+std::abs(distant[1])<.0001f,"maximum distance attenuates to silence");
    Engine::AudioListener listener; listener.position={0,0,24}; Require(audio.SetListener(listener),"moving listener");
    const auto nearMatrix=audio.OutputMatrix(sound); Require(nearMatrix[0]+nearMatrix[1]>.1f,"listener position changes attenuation");
    const auto before=audio.BufferedBytes(sound); Require(before>0&&before<48000*2*30/2,"stream retains only small chunks");
    Require(audio.Pause(sound,true),"stream pause"); audio.Update(); Require(audio.IsPlaying(sound),"paused stream retains queued chunks");
    Require(audio.Pause(sound,false),"stream resume");
    for(int i=0;i<30;++i) {std::this_thread::sleep_for(std::chrono::milliseconds(10)); audio.Update(); Require(audio.IsPlaying(sound),"stream pumped without underrun");}
    listener.volume=.6f;Require(audio.SetListener(listener)&&audio.PauseOutput(true)&&audio.OutputVolume()==0,"scene output pause mutes reverb tail independently of mixer");
    listener.volume=.4f;Require(audio.SetListener(listener)&&audio.OutputVolume()==0,"listener update retains paused master output");
    Require(audio.PauseOutput(false)&&std::abs(audio.OutputVolume()-.4f)<.0001f,"resume restores current listener gain");
    Require(audio.BufferedBytes(sound)<12*1024*1024,"bounded streaming memory"); audio.Stop(sound); Require(audio.BufferedBytes(sound)==0&&!audio.IsPlaying(sound),"stop releases submitted chunks");
    Require(audio.Play(sound,true),"stream rewind and replay"); audio.Unload(sound);
    SceneRuntime::SceneLayout layout;
    SceneRuntime::ScenePlacement parent; parent.id="parent"; parent.position={10,0,0};
    SceneRuntime::ScenePlacement source; source.id="source"; source.parentId="parent"; source.position={-15,0,5}; source.audioSource.emplace();
    source.audioSource->clip="Assets/Audio/long.wav"; source.audioSource->spatial=true; source.audioSource->streaming=true; source.audioSource->volume=0; source.audioSource->bus="Music"; source.audioSource->playOnAwake=true; source.audioSource->loop=true;
    SceneRuntime::ScenePlacement ears; ears.id="ears"; ears.audioListener.emplace();
    SceneRuntime::ScenePlacement mix; mix.id="mix"; mix.audioMixer.emplace(); mix.audioMixer->groups.push_back({"Music","music",.4f,.8f,.2f,false});
    layout.objects={parent,source,ears,mix}; SceneRuntime::SceneAudio scene; std::string error; Require(scene.Initialize(root,layout,error),"scene spatial streaming initialize");
    scene.Update(layout,{}); Require(scene.IsPlaying("source")&&scene.OutputMatrix("source")[0]>scene.OutputMatrix("source")[1],"world hierarchy left spatial awake");
    Require(scene.Reconcile(root,layout,error)&&scene.IsPlaying("source"),"scene reconciliation preserves live streaming voice");
    layout.objects[0].position={20,0,0}; scene.Update(layout,{}); Require(scene.OutputMatrix("source")[1]>scene.OutputMatrix("source")[0],"parent movement updates 3D output");
    scene.Update(layout,{},false); Require(scene.IsPlaying("source"),"inactive scene pauses stream"); scene.Update(layout,{},true);
    layout.objects[1].audioSource->enabled=false; scene.Update(layout,{}); Require(!scene.IsPlaying("source")&&scene.BufferedBytes("source")==0,"disabled source unloads stream");
}
/// <summary>音響機能回帰を実行します。</summary>
inline void Run() {Schema(); Playback();}
}
