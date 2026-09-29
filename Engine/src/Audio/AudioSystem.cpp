#include <Engine/Audio/AudioSystem.h>
#include <Engine/Core/Log.h>
#include <Engine/Audio/AudioDecoder.h>
#include <mfapi.h>

#include <cmath>
#include <format>
#include <utility>

#pragma comment(lib, "xaudio2.lib")
#pragma comment(lib, "ole32.lib")

namespace
{
    bool Check(HRESULT result, const char* operation)
    {
        if (FAILED(result))
        {
            Engine::Log::Error(std::format("{} failed: 0x{:08X}", operation, static_cast<unsigned long>(result)));
            return false;
        }
        return true;
    }
}

namespace Engine
{
    AudioSystem::~AudioSystem()
    {
        Shutdown();
    }

    bool AudioSystem::Initialize()
    {
        if (engine_) return false;
        const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (com != RPC_E_CHANGED_MODE && !Check(com, "Initialize audio COM")) return false;
        ownsCom_ = SUCCEEDED(com);
        if (!Check(MFStartup(MF_VERSION), "Initialize Media Foundation"))
        {
            Shutdown();
            return false;
        }
        ownsMediaFoundation_ = true;
        if (!Check(XAudio2Create(&engine_), "Create XAudio2") ||
            !Check(engine_->CreateMasteringVoice(&masteringVoice_), "Create mastering voice"))
        {
            Shutdown();
            return false;
        }
        Log::Info("XAudio2 initialized.");
        return true;
    }

    void AudioSystem::Shutdown()
    {
        for (auto& [handle, sound] : sounds_)
        {
            (void)handle;
            if (sound.voice != nullptr)
            {
                sound.voice->DestroyVoice();
                sound.voice = nullptr;
            }
        }
        sounds_.clear();
        if (masteringVoice_ != nullptr)
        {
            masteringVoice_->DestroyVoice();
            masteringVoice_ = nullptr;
        }
        engine_.Reset();
        if (ownsMediaFoundation_)
        {
            Check(MFShutdown(), "Shutdown Media Foundation");
            ownsMediaFoundation_ = false;
        }
        if (ownsCom_)
        {
            CoUninitialize();
            ownsCom_ = false;
        }
    }

    SoundHandle AudioSystem::Load(const std::filesystem::path& path)
    {
        if (!engine_ || nextHandle_ == 0) return 0;
        Sound sound;
        if (!DecodeAudioFile(path, sound.wave)) return 0;
        const SoundHandle handle = nextHandle_++;
        sounds_.emplace(handle, std::move(sound));
        return handle;
    }

    bool AudioSystem::Play(SoundHandle handle, bool loop)
    {
        const auto found = sounds_.find(handle);
        if (!engine_ || found == sounds_.end()) return false;
        Stop(handle);
        auto& sound = found->second;
        if (!Check(engine_->CreateSourceVoice(&sound.voice, &sound.wave.format), "Create source voice")) return false;
        XAUDIO2_BUFFER buffer{};
        buffer.Flags = XAUDIO2_END_OF_STREAM;
        buffer.AudioBytes = static_cast<UINT32>(sound.wave.samples.size());
        buffer.pAudioData = sound.wave.samples.data();
        buffer.LoopCount = loop ? XAUDIO2_LOOP_INFINITE : 0;
        if (!Check(sound.voice->SetVolume(sound.volume), "Set sound volume") ||
            !Check(sound.voice->SubmitSourceBuffer(&buffer), "Submit sound buffer") ||
            !Check(sound.voice->Start(), "Start sound"))
        {
            Stop(handle);
            return false;
        }
        return true;
    }

    void AudioSystem::Stop(SoundHandle handle)
    {
        const auto found = sounds_.find(handle);
        if (found != sounds_.end() && found->second.voice != nullptr)
        {
            // DestroyVoice が音声スレッドの参照終了を待つため、この後で波形を解放できます。
            found->second.voice->DestroyVoice();
            found->second.voice = nullptr;
        }
    }

    void AudioSystem::Unload(SoundHandle handle)
    {
        Stop(handle);
        sounds_.erase(handle);
    }

    bool AudioSystem::SetVolume(SoundHandle handle, float volume)
    {
        const auto found = sounds_.find(handle);
        if (found == sounds_.end() || !std::isfinite(volume) || volume < 0.0f || volume > 1.0f) return false;
        if (found->second.voice != nullptr && !Check(found->second.voice->SetVolume(volume), "Set sound volume")) return false;
        found->second.volume = volume;
        return true;
    }

    bool AudioSystem::IsPlaying(SoundHandle handle) const
    {
        const auto found = sounds_.find(handle);
        if (found == sounds_.end() || found->second.voice == nullptr) return false;
        XAUDIO2_VOICE_STATE state{};
        found->second.voice->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
        return state.BuffersQueued != 0;
    }
}
