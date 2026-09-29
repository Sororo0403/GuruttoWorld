#pragma once

#if defined(_DEBUG) || defined(ENGINE_DEVELOPMENT)
#include <Engine/Audio/AudioSystem.h>

namespace App
{
    class AudioPanel final
    {
    public:
        /// <summary>
        /// サンプル WAV の再生・停止・ループ・音量を操作するパネルを描画します。
        /// </summary>
        static void Draw(Engine::AudioSystem& audio, Engine::SoundHandle sound);
    };
}
#endif
