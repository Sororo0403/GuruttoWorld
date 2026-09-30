#pragma once
#include <Engine/Audio/AudioSystem.h>
#include "TitleMenu.h"
#include <array>

namespace App
{
    class TitleAudio final
    {
    public:
        /// <summary>操作・音量・フォーカス・遷移に合わせてBGMと効果音を更新します。初回に音源を読み込みます。</summary>
        void Update(const std::filesystem::path& root, const TitleMenu& menu, bool active, double deltaSeconds);
    private:
        Engine::AudioSystem audio_;
        std::array<Engine::SoundHandle, 5> sounds_{};
        bool attempted_ = false;
        float fade_ = 0.0f;
    };
}
