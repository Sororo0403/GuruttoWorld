#pragma once

#include <Engine/Audio/WaveData.h>
#include <xaudio2.h>
#include <wrl/client.h>
#include <cstdint>
#include <map>

namespace Engine
{
    using SoundHandle = std::uint64_t;

    // すべての操作と破棄を初期化したスレッドから行ってください。
    class AudioSystem final
    {
    public:
        /// <summary>
        /// サウンド管理を生成します。
        /// </summary>
        AudioSystem() = default;

        /// <summary>
        /// 再生を停止し、音声・XAudio2・COM を順に解放します。
        /// </summary>
        ~AudioSystem();

        /// <summary>
        /// 音声リソースのコピー生成を禁止します。
        /// </summary>
        AudioSystem(const AudioSystem&) = delete;

        /// <summary>
        /// 音声リソースのコピー代入を禁止します。
        /// </summary>
        AudioSystem& operator=(const AudioSystem&) = delete;

        /// <summary>
        /// COM と XAudio2、既定の出力デバイスを初期化します。失敗後も再試行できます。
        /// </summary>
        bool Initialize();

        /// <summary>
        /// ソースボイスの破棄完了後に波形を解放します。複数回呼び出せます。
        /// </summary>
        void Shutdown();

        /// <summary>
        /// PCM WAV を読み込みます。失敗時は無効ハンドル 0 を返します。
        /// </summary>
        SoundHandle Load(const std::filesystem::path& path);

        /// <summary>
        /// 指定した音を先頭から再生します。同じハンドルの既存再生は停止します。
        /// </summary>
        /// <param name="handle">Load で取得したハンドル。</param>
        /// <param name="loop">true の場合は停止するまでループします。</param>
        bool Play(SoundHandle handle, bool loop = false);

        /// <summary>
        /// 指定した音の再生を停止し、ボイスを破棄します。波形は保持します。
        /// </summary>
        void Stop(SoundHandle handle);

        /// <summary>
        /// 指定した音を停止して波形も解放します。
        /// </summary>
        void Unload(SoundHandle handle);

        /// <summary>
        /// 音量を 0～1 で指定します。停止中の設定は次の再生にも適用します。
        /// </summary>
        bool SetVolume(SoundHandle handle, float volume);

        /// <summary>
        /// 指定した音に再生待ちまたは再生中のバッファーがあるか取得します。
        /// </summary>
        bool IsPlaying(SoundHandle handle) const;

    private:
        struct Sound
        {
            WaveData wave;
            // XAudio2 のボイスは COM ではなく DestroyVoice で解放します。
            IXAudio2SourceVoice* voice = nullptr;
            float volume = 1.0f;
        };

        Microsoft::WRL::ComPtr<IXAudio2> engine_;
        IXAudio2MasteringVoice* masteringVoice_ = nullptr;
        std::map<SoundHandle, Sound> sounds_;
        SoundHandle nextHandle_ = 1;
        bool ownsCom_ = false;
    };
}
