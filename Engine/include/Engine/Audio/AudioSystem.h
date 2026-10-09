#pragma once

#include <Engine/Audio/WaveData.h>
#include <Engine/Audio/AudioStream.h>
#include <x3daudio.h>
#include <deque>
#include <array>
#include <string>
#include <xaudio2.h>
#include <wrl/client.h>
#include <cstdint>
#include <map>

namespace Engine
{
    using SoundHandle = std::uint64_t;

    struct AudioListener {
        std::array<float,3> position{},front{0,0,1},up{0,1,0}; float volume=1;
    };
    struct AudioSpatialSettings {
        std::array<float,3> position{}; bool enabled=false;
        float minimumDistance=1,maximumDistance=100,blend=1,pitch=1,lowPass=1;
    };
    struct AudioBusSettings {float volume=1,lowPass=1,reverb=0; bool mute=false;};
    // すべての操作と破棄を初期化したスレッドから行ってください。
    class AudioSystem final
    {
    public:
        /// <summary>
        /// サウンド管理を生成します。
        /// </summary>
        AudioSystem() = default;

        /// <summary>
        /// 再生を停止し、音声・XAudio2・Media Foundation・COM を順に解放します。
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
        /// COM・Media Foundation と XAudio2、既定の出力デバイスを初期化します。失敗後も再試行できます。
        /// </summary>
        bool Initialize();

        /// <summary>
        /// ソースボイスの破棄完了後に波形を解放します。複数回呼び出せます。
        /// </summary>
        void Shutdown();

        /// <summary>
        /// WAV・MP3・AAC などを Media Foundation で PCM に展開して保持します。
        /// 対応形式は OS のデコーダーに依存します。展開後 64 MiB まで。失敗時は無効ハンドル 0 を返します。
        /// </summary>
        SoundHandle Load(const std::filesystem::path& path,bool streaming=false);
        /// <summary>リスナーの位置・向き・全体音量を設定します。</summary>
        bool SetListener(const AudioListener& listener);
        /// <summary>全体の出力だけを消音し、リスナー音量とミキサー設定を保持します。</summary>
        bool PauseOutput(bool paused);
        /// <summary>実際のmastering voiceの出力音量を診断用に取得します。</summary>
        float OutputVolume() const;
        /// <summary>音源の3D位置・距離減衰・ピッチ・低域フィルタを設定します。</summary>
        bool SetSpatial(SoundHandle handle,const AudioSpatialSettings& settings);
        /// <summary>ミキサーバスの音量・消音・低域フィルタ・リバーブを設定します。</summary>
        bool SetBus(const std::string& name,const AudioBusSettings& settings);
        /// <summary>音源を指定したミキサーバスへ接続します。</summary>
        bool Route(SoundHandle handle,const std::string& bus);
        /// <summary>不要なバスを削除し、接続中の音源をMasterへ戻します。</summary>
        bool RemoveBus(const std::string& name);
        /// <summary>ストリームへ少数チャンクを補充し、3D音響を更新します。毎フレーム呼び出してください。</summary>
        void Update();
        /// <summary>テスト・診断用の実際の出力チャンネル行列を取得します。</summary>
        std::vector<float> OutputMatrix(SoundHandle handle) const;
        /// <summary>ストリームで保持中のPCMバイト数を取得します。</summary>
        size_t BufferedBytes(SoundHandle handle) const;

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
        /// 指定した音の現在の音量を取得します。無効なハンドルは 0 を返します。
        /// </summary>
        float GetVolume(SoundHandle handle) const;

        /// <summary>
        /// 指定した音に再生待ちまたは再生中のバッファーがあるか取得します。
        /// </summary>
        bool IsPlaying(SoundHandle handle) const;
        /// <summary>キューを保持して音源を一時停止または再開します。</summary>
        bool Pause(SoundHandle handle, bool paused);

    private:
        struct Sound
        {
            WaveData wave;
            // XAudio2 のボイスは COM ではなく DestroyVoice で解放します。
            IXAudio2SourceVoice* voice = nullptr;
            float volume = 1.0f;
            AudioStream stream;
            bool streaming=false,loop=false,ended=false;
            std::deque<std::vector<unsigned char>> chunks;
            std::string bus="Master";
            AudioSpatialSettings spatial;
            std::vector<float> matrix;
        };

        struct Bus {IXAudio2SubmixVoice* voice=nullptr; AudioBusSettings settings;};
        /// <summary>ボイスの実際の出力行列とフィルタを更新します。</summary>
        bool ApplySpatial(Sound& sound);
        /// <summary>ストリーミングの送信済みチャンクを解放し補充します。</summary>
        bool FillStream(Sound& sound);
        /// <summary>実際のミキサーsubmixボイスを作成します。</summary>
        bool EnsureBus(const std::string& name);
        /// <summary>次のデコードチャンクを送信します。</summary>
        bool QueueStreamChunk(Sound& sound);
        std::map<std::string,Bus> buses_;
        AudioListener listener_;
        bool outputPaused_=false;
        X3DAUDIO_HANDLE spatialHandle_{};
        UINT32 outputChannels_=2,outputSampleRate_=48000;
        Microsoft::WRL::ComPtr<IXAudio2> engine_;
        IXAudio2MasteringVoice* masteringVoice_ = nullptr;
        std::map<SoundHandle, Sound> sounds_;
        SoundHandle nextHandle_ = 1;
        bool ownsCom_ = false;
        bool ownsMediaFoundation_ = false;
    };
}
