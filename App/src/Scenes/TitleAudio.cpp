#include "TitleAudio.h"
#include <Engine/Core/Log.h>
#include <algorithm>
#include <cmath>

namespace App
{
    void TitleAudio::Update(const std::filesystem::path& root, const TitleMenu& menu, bool active, double deltaSeconds)
    {
        if (!active)
        {
            for (const auto sound : sounds_) audio_.Stop(sound);
            fade_ = 0.0f;
            return;
        }
        if (!attempted_)
        {
            attempted_ = true;
            if (!audio_.Initialize())
            {
                Engine::Log::Error("Title audio unavailable; continuing without audio.");
                return;
            }
            constexpr std::array<const char*, 5> Files{ "Bgm.wav", "Select.wav", "Confirm.wav", "Back.wav", "Error.wav" };
            for (size_t i = 0; i < Files.size(); ++i)
            {
                sounds_[i] = audio_.Load(root / "Assets/Audio/Title" / Files[i]);
                if (!sounds_[i]) Engine::Log::Error("Title sound asset could not be loaded; continuing without this sound.");
            }
        }
        const float elapsed = std::isfinite(deltaSeconds) ? static_cast<float>(std::clamp(deltaSeconds, 0.0, 0.1)) : 0.0f;
        fade_ = std::min(1.0f, fade_ + elapsed / 0.4f);
        const float volume = static_cast<float>(menu.GetSettings().volume) / 10.0f;
        const float musicVolume = volume * 0.30f * fade_ * (1.0f - menu.TransitionProgress());
        audio_.SetVolume(sounds_[0], musicVolume);
        if (menu.TransitionProgress() >= 1.0f) audio_.Stop(sounds_[0]);
        else if (sounds_[0] && !audio_.IsPlaying(sounds_[0])) audio_.Play(sounds_[0], true);
        for (size_t i = 1; i < sounds_.size(); ++i) audio_.SetVolume(sounds_[i], volume * 0.35f);
        const auto cue = static_cast<size_t>(menu.GetCue());
        if (cue > 0 && cue < sounds_.size() && sounds_[cue] && volume > 0.0f) audio_.Play(sounds_[cue]);
    }
}
