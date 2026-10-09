#pragma once
#include <Engine/Audio/WaveData.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>
namespace Engine {
class AudioStream final {
public:
    /// <summary>音源を開き、全体を展開せずPCM形式を取得します。</summary>
    bool Open(const std::filesystem::path& path);
    /// <summary>次のPCMチャンクを取得します。終端で空チャンクとended=trueを返します。</summary>
    bool Read(std::vector<unsigned char>& bytes,bool& ended);
    /// <summary>デコーダーを先頭へ戻します。</summary>
    bool Rewind();
    /// <summary>PCM形式を取得します。</summary>
    const WAVEFORMATEX& Format() const {return format_;}
private:
    Microsoft::WRL::ComPtr<IMFSourceReader> reader_;
    WAVEFORMATEX format_{};
};
}
