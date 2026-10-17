#include <SceneRuntime/SceneAudio.h>
#include <SceneRuntime/SceneTransforms.h>
#include <SceneRuntime/SceneWorld.h>
#include <algorithm>
#include <Engine/Core/Log.h>
#include <Engine/Core/Profiler.h>
namespace
{
    bool AudioEnabled(const SceneRuntime::ScenePlacement& p)
    {
        return p.audioSource && p.audioSource->enabled && !p.audioSource->clip.empty();
    }
    std::optional<size_t> SelectListener(const SceneRuntime::SceneLayout& layout)
    {
        std::optional<size_t> fallback;
        for (size_t i = 0; i < layout.objects.size(); ++i)
        {
            const auto& p = layout.objects[i];
            if (p.audioListener && p.audioListener->enabled)
                return i;
            if (p.camera && p.camera->enabled && (p.id == layout.settings.mainCamera || !fallback))
                fallback = i;
        }
        return fallback;
    }
}
namespace SceneRuntime
{
    bool SceneAudio::Initialize(const std::filesystem::path& root, const SceneLayout& layout, std::string& error)
    {
        Stop();
        audio_.Shutdown();
        sources_.clear();
        cues_.clear();
        configuredBuses_.clear();
        configuration_.clear();
        configured_ = false;
        paused_ = false;
        initialized_ = false;
        root_ = root;
        return Refresh(layout, error);
    }
    bool SceneAudio::Mix(const SceneLayout& layout, const UiState& state)
    {
        std::map<std::string, Engine::AudioBusSettings> groups{{"Master", {}}};
        for (const auto& p : layout.objects)
            if (p.audioMixer && p.audioMixer->enabled)
                for (const auto& g : p.audioMixer->groups)
                {
                    groups[g.name] = {std::clamp(g.volume * state.Value(g.volumeBinding, 1), 0.0f, 1.0f), g.lowPass,
                                      g.reverb, g.mute};
                }
        for (const auto& p : layout.objects)
            if (p.audioSource && p.audioSource->enabled)
                groups.try_emplace(p.audioSource->bus);
        for (const auto& name : configuredBuses_)
            if (!groups.contains(name))
                audio_.RemoveBus(name);
        for (const auto& [name, settings] : groups)
            if (!audio_.SetBus(name, settings))
                return false;
        configuredBuses_.clear();
        for (const auto& [name, settings] : groups)
        {
            static_cast<void>(settings);
            configuredBuses_.insert(name);
        }
        return true;
    }
    bool SceneAudio::PrepareSource(const ScenePlacement& p, std::map<std::string, Source>& pending,
                                   std::vector<Engine::SoundHandle>& loaded)
    {
        const auto& c = *p.audioSource;
        const auto old = sources_.find(p.id);
        if (old != sources_.end() && old->second.clip == c.clip && old->second.loop == c.loop &&
            old->second.streaming == c.streaming)
            pending[p.id] = old->second;
        else
        {
            const auto h = audio_.Load(root_ / c.clip, c.streaming);
            if (!h)
                return false;
            loaded.push_back(h);
            audio_.SetVolume(h, c.volume);
            pending[p.id] = {h, c.loop, c.playOnAwake, false, c.streaming, c.clip};
        }
        pending[p.id].awake = c.playOnAwake;
        return audio_.Route(pending[p.id].handle, c.bus);
    }
    bool SceneAudio::SameConfiguration(const SceneLayout& layout) const
    {
        if (!configured_)
            return false;
        size_t index = 0;
        for (const auto& p : layout.objects)
        {
            if (!p.audioSource && !p.audioMixer)
                continue;
            if (index >= configuration_.size())
                return false;
            const auto& previous = configuration_[index++];
            if (p.id != previous.id || p.audioSource != previous.source || p.audioMixer != previous.mixer)
                return false;
        }
        return index == configuration_.size();
    }
    void SceneAudio::UnloadReplacedSources(const std::map<std::string, Source>& pending)
    {
        for (const auto& [id, s] : sources_)
        {
            const auto replacement = pending.find(id);
            if (replacement == pending.end() || replacement->second.handle != s.handle)
                audio_.Unload(s.handle);
        }
    }
    void SceneAudio::CaptureConfiguration(const SceneLayout& layout)
    {
        configuration_.clear();
        for (const auto& p : layout.objects)
            if (p.audioSource || p.audioMixer)
                configuration_.push_back({p.id, p.audioSource, p.audioMixer});
    }
    bool SceneAudio::Refresh(const SceneLayout& layout, std::string& error, const SceneWorld* world)
    {
        if (SameConfiguration(layout))
        {
            Spatial(layout, world);
            error.clear();
            return true;
        }
        // 失敗した候補が現在の音声経路を変更している可能性があるため、復旧時は最後に成功した設定を再適用します。
        configured_ = false;
        Engine::CpuScope scope("Audio source reconciliation");
        bool needed = false;
        for (const auto& p : layout.objects)
            needed |= AudioEnabled(p);
        if (needed && !initialized_)
        {
            if (!audio_.Initialize())
            {
                error = "Audio output unavailable";
                return false;
            }
            initialized_ = true;
        }
        if (initialized_ && !Mix(layout, {}))
        {
            error = "Audio mixer settings invalid";
            return false;
        }
        std::map<std::string, Source> pending;
        std::map<std::string, std::string> cues;
        std::vector<Engine::SoundHandle> loaded;
        for (const auto& p : layout.objects)
        {
            if (!AudioEnabled(p))
                continue;
            if (!PrepareSource(p, pending, loaded))
            {
                for (const auto handle : loaded)
                    audio_.Unload(handle);
                error = "Cannot load audio: " + p.id;
                return false;
            }
            const auto& c = *p.audioSource;
            if (!c.cue.empty())
                cues.try_emplace(c.cue, p.id);
        }
        UnloadReplacedSources(pending);
        sources_ = std::move(pending);
        cues_ = std::move(cues);
        CaptureConfiguration(layout);
        configured_ = true;
        Spatial(layout, world);
        error.clear();
        return true;
    }
    void SceneAudio::Spatial(const SceneLayout& layout, const SceneWorld* world)
    {
        if (sources_.empty())
            return;
        if (world && &world->Layout() != &layout)
            world = nullptr;
        std::vector<DirectX::XMFLOAT4X4> matrices;
        if (!world)
        {
            std::string error;
            Engine::CpuScope scope("Audio transform resolution");
            if (!SceneTransforms::Resolve(layout, matrices, error))
            {
                Engine::Log::Warning(error);
                return;
            }
        }
        const auto matrix = [&](size_t index, DirectX::XMFLOAT4X4& result)
        {
            if (world)
                return world->WorldMatrix(layout.objects[index].id, result);
            result = matrices[index];
            return true;
        };
        Engine::AudioListener listener;
        const auto selected = SelectListener(layout);
        if (selected)
        {
            DirectX::XMFLOAT4X4 m;
            if (!matrix(*selected, m))
                return;
            listener.position = {m._41, m._42, m._43};
            const auto front = DirectX::XMVector3Normalize(DirectX::XMVectorSet(m._31, m._32, m._33, 0));
            const auto right = DirectX::XMVector3Normalize(
                DirectX::XMVector3Cross(DirectX::XMVectorSet(m._21, m._22, m._23, 0), front));
            const auto up = DirectX::XMVector3Normalize(DirectX::XMVector3Cross(front, right));
            DirectX::XMFLOAT3 f, u;
            DirectX::XMStoreFloat3(&f, front);
            DirectX::XMStoreFloat3(&u, up);
            listener.front = {f.x, f.y, f.z};
            listener.up = {u.x, u.y, u.z};
            const auto& p = layout.objects[*selected];
            if (p.audioListener && p.audioListener->enabled)
                listener.volume = p.audioListener->volume;
        }
        audio_.SetListener(listener);
        for (size_t i = 0; i < layout.objects.size(); ++i)
        {
            const auto& p = layout.objects[i];
            const auto found = sources_.find(p.id);
            if (found == sources_.end() || !p.audioSource)
                continue;
            const auto& c = *p.audioSource;
            DirectX::XMFLOAT4X4 m;
            if (!matrix(i, m))
                return;
            Engine::AudioSpatialSettings settings{
                {m._41, m._42, m._43}, c.spatial, c.minimumDistance, c.maximumDistance,
                c.spatialBlend,        c.pitch,   c.lowPass};
            audio_.SetSpatial(found->second.handle, settings);
        }
    }
    void SceneAudio::Update(const SceneLayout& layout, const UiState& state, bool active, const SceneWorld* world)
    {
        std::string error;
        if (!Refresh(layout, error, world))
            Engine::Log::Warning(error);
        Pause(!active);
        if (!active)
            return;
        if (initialized_)
            Mix(layout, state);
        for (const auto& p : layout.objects)
        {
            const auto i = sources_.find(p.id);
            if (i == sources_.end() || !p.audioSource)
                continue;
            const auto& c = *p.audioSource;
            auto& s = i->second;
            audio_.SetVolume(s.handle, std::clamp(c.volume * state.Value(c.volumeBinding, 1), 0.0f, 1.0f));
            if (c.enabled && s.awake && !s.started)
                s.started = audio_.Play(s.handle, s.loop);
            if (!c.enabled)
                audio_.Stop(s.handle);
        }
        audio_.Update();
    }
    bool SceneAudio::Play(const std::string& object)
    {
        const auto i = sources_.find(object);
        if (i == sources_.end() || !audio_.Play(i->second.handle, i->second.loop))
            return false;
        if (paused_)
            audio_.Pause(i->second.handle, true);
        i->second.started = true;
        return true;
    }
    bool SceneAudio::IsPlaying(const std::string& object) const
    {
        const auto i = sources_.find(object);
        return i != sources_.end() && audio_.IsPlaying(i->second.handle);
    }
    float SceneAudio::Volume(const std::string& object) const
    {
        const auto i = sources_.find(object);
        return i == sources_.end() ? 0 : audio_.GetVolume(i->second.handle);
    }
    std::vector<float> SceneAudio::OutputMatrix(const std::string& object) const
    {
        const auto i = sources_.find(object);
        return i == sources_.end() ? std::vector<float>{} : audio_.OutputMatrix(i->second.handle);
    }
    size_t SceneAudio::BufferedBytes(const std::string& object) const
    {
        const auto i = sources_.find(object);
        return i == sources_.end() ? 0 : audio_.BufferedBytes(i->second.handle);
    }
    void SceneAudio::Cue(const std::string& cue)
    {
        const auto i = cues_.find(cue);
        if (i != cues_.end())
            Play(i->second);
    }
    void SceneAudio::Pause(bool paused)
    {
        audio_.PauseOutput(paused);
        if (paused_ == paused)
            return;
        paused_ = paused;
        for (const auto& [id, s] : sources_)
        {
            static_cast<void>(id);
            audio_.Pause(s.handle, paused);
        }
    }
    void SceneAudio::Stop()
    {
        for (auto& [id, s] : sources_)
        {
            static_cast<void>(id);
            audio_.Stop(s.handle);
            s.started = false;
        }
    }
}
