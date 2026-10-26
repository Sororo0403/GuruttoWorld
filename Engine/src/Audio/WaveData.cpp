#include <Engine/Audio/WaveData.h>
#include <Engine/Audio/AudioDecoder.h>
#include <Engine/Core/Log.h>
#include <mfapi.h>

#pragma comment(lib, "ole32.lib")

namespace
{
    class DecoderSession final
    {
    public:
        DecoderSession()
        {
            const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            if (FAILED(com) && com != RPC_E_CHANGED_MODE)
            {
                Engine::Log::Error("Cannot initialize COM for WAV decoding.");
                return;
            }
            ownsCom_ = SUCCEEDED(com);
            ready_ = SUCCEEDED(MFStartup(MF_VERSION));
            if (!ready_)
            {
                Engine::Log::Error("Cannot initialize Media Foundation for WAV decoding.");
            }
        }

        ~DecoderSession()
        {
            if (ready_)
            {
                MFShutdown();
            }
            if (ownsCom_)
            {
                CoUninitialize();
            }
        }

        DecoderSession(const DecoderSession&) = delete;
        DecoderSession& operator=(const DecoderSession&) = delete;

        bool Ready() const
        {
            return ready_;
        }

    private:
        bool ownsCom_ = false;
        bool ready_ = false;
    };
}

namespace Engine
{
    bool LoadWaveFile(const std::filesystem::path& path, WaveData& wave)
    {
        std::error_code error;
        const auto size = std::filesystem::file_size(path, error);
        if (error || size > 64 * 1024 * 1024)
        {
            Log::Error("Cannot read WAV file or source exceeds 64 MiB.");
            return false;
        }
        // 互換用の入口では初期化不要の契約を保ち、呼び出し元の COM と MF の所有権を変えません。
        DecoderSession session;
        return session.Ready() && DecodeAudioFile(path, wave);
    }
}
