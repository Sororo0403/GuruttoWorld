#include <Engine/Audio/AudioSystem.h>
#include <Engine/Core/Log.h>
#include <xaudio2fx.h>
#include <algorithm>
#include <cmath>
namespace {
bool Unit(float value) {return std::isfinite(value)&&value>=0&&value<=1;}
bool Finite(const std::array<float,3>& value) {return std::all_of(value.begin(),value.end(),[](float v){return std::isfinite(v)&&std::abs(v)<=1000000;});}
bool ReadBuffered(Engine::AudioStream& stream,std::vector<unsigned char>& bytes,bool& ended) {
    const size_t target=std::clamp<size_t>(stream.Format().nAvgBytesPerSec/4,4096,256*1024);
    for(size_t attempt=0;bytes.size()<target&&!ended&&attempt<128;++attempt) {
        std::vector<unsigned char> sample;
        if(!stream.Read(sample,ended)||sample.size()>4*1024*1024-bytes.size()) return false;
        bytes.insert(bytes.end(),sample.begin(),sample.end());
    }
    if(!ended&&bytes.empty()) {Engine::Log::Error("Audio stream produced no data within sample limit");return false;}
    return true;
}
X3DAUDIO_VECTOR Vector(const std::array<float,3>& value) {return {value[0],value[1],value[2]};}
bool Orientation(const Engine::AudioListener& listener) {
    const auto& a=listener.front; const auto& b=listener.up;
    const float lengthA=a[0]*a[0]+a[1]*a[1]+a[2]*a[2],lengthB=b[0]*b[0]+b[1]*b[1]+b[2]*b[2];
    const float dot=a[0]*b[0]+a[1]*b[1]+a[2]*b[2];
    return std::abs(lengthA-1)<.001f&&std::abs(lengthB-1)<.001f&&std::abs(dot)<.001f;
}
}
namespace Engine {
bool AudioSystem::SetListener(const AudioListener& listener) {
    if(!Finite(listener.position)||!Finite(listener.front)||!Finite(listener.up)||!Unit(listener.volume)||!Orientation(listener)) return false;
    listener_=listener;
    if(masteringVoice_ && FAILED(masteringVoice_->SetVolume(outputPaused_?0:listener.volume))) return false;
    for(auto& [handle,sound]:sounds_) {static_cast<void>(handle); if(!ApplySpatial(sound)) return false;}
    return true;
}
bool AudioSystem::PauseOutput(bool paused) {
    if(masteringVoice_&&FAILED(masteringVoice_->SetVolume(paused?0:listener_.volume))) return false;
    outputPaused_=paused;return true;
}
float AudioSystem::OutputVolume() const {
    float volume=0;if(masteringVoice_) masteringVoice_->GetVolume(&volume);return volume;
}
bool AudioSystem::SetSpatial(SoundHandle handle,const AudioSpatialSettings& settings) {
    const auto found=sounds_.find(handle);
    if(found==sounds_.end()||!Finite(settings.position)||!Unit(settings.blend)||!Unit(settings.lowPass)||
        !std::isfinite(settings.minimumDistance)||!std::isfinite(settings.maximumDistance)||settings.minimumDistance<=0||
        settings.maximumDistance<=settings.minimumDistance||settings.maximumDistance>1000000||
        !std::isfinite(settings.pitch)||settings.pitch<.25f||settings.pitch>2) return false;
    found->second.spatial=settings; return ApplySpatial(found->second);
}
bool AudioSystem::EnsureBus(const std::string& name) {
    auto found=buses_.find(name);
    if(found==buses_.end()) {
        if(buses_.size()>=64) return false;
        Microsoft::WRL::ComPtr<IUnknown> reverb;
        if(FAILED(XAudio2CreateReverb(&reverb))) return false;
        XAUDIO2_EFFECT_DESCRIPTOR effect{reverb.Get(),TRUE,outputChannels_}; XAUDIO2_EFFECT_CHAIN chain{1,&effect};
        Bus bus;
        XAUDIO2_SEND_DESCRIPTOR send{0,buses_.contains("Master")?buses_.at("Master").voice:nullptr};
        XAUDIO2_VOICE_SENDS sends{1,&send};
        const bool master=name=="Master";
        if(!master&&!send.pOutputVoice) return false;
        if(FAILED(engine_->CreateSubmixVoice(&bus.voice,outputChannels_,outputSampleRate_,XAUDIO2_VOICE_USEFILTER,master?1:0,master?nullptr:&sends,&chain))) return false;
        found=buses_.emplace(name,bus).first;
    }
    return true;
}
bool AudioSystem::SetBus(const std::string& name,const AudioBusSettings& settings) {
    if(!engine_||name.empty()||name.size()>128||name.find('\0')!=std::string::npos||
        !Unit(settings.volume)||!Unit(settings.lowPass)||!Unit(settings.reverb)) return false;
    if(!EnsureBus(name)) return false;
    const auto found=buses_.find(name);
    auto& bus=found->second;
    XAUDIO2FX_REVERB_I3DL2_PARAMETERS preset=XAUDIO2FX_I3DL2_PRESET_ROOM;
    XAUDIO2FX_REVERB_PARAMETERS parameters{}; ReverbConvertI3DL2ToNative(&preset,&parameters);
    parameters.WetDryMix=settings.reverb*100;
    XAUDIO2_FILTER_PARAMETERS filter{LowPassFilter,settings.lowPass,1};
    if(FAILED(bus.voice->SetVolume(settings.mute?0:settings.volume))||FAILED(bus.voice->SetFilterParameters(&filter))||
        FAILED(bus.voice->SetEffectParameters(0,&parameters,sizeof(parameters)))) return false;
    bus.settings=settings; return true;
}
bool AudioSystem::Route(SoundHandle handle,const std::string& bus) {
    const auto sound=sounds_.find(handle); const auto target=buses_.find(bus);
    if(sound==sounds_.end()||target==buses_.end()) return false;
    if(sound->second.voice) {
        XAUDIO2_SEND_DESCRIPTOR send{0,target->second.voice}; XAUDIO2_VOICE_SENDS sends{1,&send};
        if(FAILED(sound->second.voice->SetOutputVoices(&sends))) return false;
    }
    sound->second.bus=bus; return ApplySpatial(sound->second);
}
bool AudioSystem::RemoveBus(const std::string& name) {
    if(name=="Master") return false;
    const auto found=buses_.find(name); if(found==buses_.end()) return true;
    for(auto& [handle,sound]:sounds_) if(sound.bus==name&&!Route(handle,"Master")) return false;
    found->second.voice->DestroyVoice(); buses_.erase(found); return true;
}
bool AudioSystem::ApplySpatial(Sound& sound) {
    const UINT32 channels=sound.wave.format.nChannels;
    sound.matrix.assign(channels*outputChannels_,0);
    for(UINT32 source=0;source<channels;++source) {
        for(UINT32 output=0;output<outputChannels_;++output) sound.matrix[source*outputChannels_+output]=channels==1?1.0f:(source==output?1.0f:0.0f);
    }
    if(sound.spatial.enabled) {
        X3DAUDIO_LISTENER listener{}; listener.Position=Vector(listener_.position); listener.OrientFront=Vector(listener_.front); listener.OrientTop=Vector(listener_.up);
        X3DAUDIO_EMITTER emitter{}; emitter.Position=Vector(sound.spatial.position); emitter.OrientFront={0,0,1}; emitter.OrientTop={0,1,0};
        emitter.ChannelCount=channels; emitter.ChannelRadius=.1f;
        float azimuths[]{3*X3DAUDIO_PI/2,X3DAUDIO_PI/2}; emitter.pChannelAzimuths=channels==1?nullptr:azimuths;
        X3DAUDIO_DISTANCE_CURVE_POINT points[]{{0,1},{sound.spatial.minimumDistance/sound.spatial.maximumDistance,1},{1,0}};
        X3DAUDIO_DISTANCE_CURVE curve{points,3}; emitter.pVolumeCurve=&curve; emitter.CurveDistanceScaler=sound.spatial.maximumDistance;
        emitter.DopplerScaler=0;
        std::vector<float> matrix(channels*outputChannels_);
        X3DAUDIO_DSP_SETTINGS dsp{}; dsp.SrcChannelCount=channels; dsp.DstChannelCount=outputChannels_; dsp.pMatrixCoefficients=matrix.data();
        X3DAudioCalculate(spatialHandle_,&listener,&emitter,X3DAUDIO_CALCULATE_MATRIX,&dsp);
        for(size_t i=0;i<matrix.size();++i) sound.matrix[i]=std::lerp(sound.matrix[i],matrix[i],sound.spatial.blend);
    }
    if(!sound.voice) return true;
    XAUDIO2_FILTER_PARAMETERS filter{LowPassFilter,sound.spatial.lowPass,1};
    return SUCCEEDED(sound.voice->SetOutputMatrix(buses_.at(sound.bus).voice,channels,outputChannels_,sound.matrix.data()))&&
        SUCCEEDED(sound.voice->SetFrequencyRatio(sound.spatial.pitch))&&SUCCEEDED(sound.voice->SetFilterParameters(&filter));
}
bool AudioSystem::QueueStreamChunk(Sound& sound) {
    std::vector<unsigned char> bytes; bool ended=false;
    if(!ReadBuffered(sound.stream,bytes,ended)) return false;
    if(ended) {
        if(sound.loop) {
            if(!sound.stream.Rewind()) return false;
            ended=false;
            if(bytes.empty() && (!ReadBuffered(sound.stream,bytes,ended)||bytes.empty())) {Log::Error("Audio stream loop seek produced empty data");return false;}
        }
        else sound.ended=true;
    }
    if(bytes.empty()) return true;
    sound.chunks.push_back(std::move(bytes)); const auto& chunk=sound.chunks.back();
    XAUDIO2_BUFFER buffer{}; buffer.AudioBytes=static_cast<UINT32>(chunk.size()); buffer.pAudioData=chunk.data();
    buffer.Flags=sound.ended?XAUDIO2_END_OF_STREAM:0;
    if(FAILED(sound.voice->SubmitSourceBuffer(&buffer))) {sound.chunks.pop_back(); return false;}
    return true;
}
bool AudioSystem::FillStream(Sound& sound) {
    XAUDIO2_VOICE_STATE state{}; sound.voice->GetState(&state,XAUDIO2_VOICE_NOSAMPLESPLAYED);
    while(sound.chunks.size()>state.BuffersQueued) sound.chunks.pop_front();
    for(size_t attempt=0;sound.chunks.size()<3&&!sound.ended&&attempt<128;++attempt) {
        if(!QueueStreamChunk(sound)) return false;
    }
    return sound.ended||!sound.chunks.empty();
}
void AudioSystem::Update() {
    for(auto& [handle,sound]:sounds_) {
        if(sound.voice&&sound.streaming&&!FillStream(sound)) {Log::Warning("Streaming audio decode failed"); Stop(handle);}
        ApplySpatial(sound);
    }
}
std::vector<float> AudioSystem::OutputMatrix(SoundHandle handle) const {const auto found=sounds_.find(handle); return found==sounds_.end()?std::vector<float>{}:found->second.matrix;}
size_t AudioSystem::BufferedBytes(SoundHandle handle) const {
    const auto found=sounds_.find(handle); if(found==sounds_.end()) return 0;
    size_t bytes=0; for(const auto& chunk:found->second.chunks) bytes+=chunk.size(); return bytes;
}
}
