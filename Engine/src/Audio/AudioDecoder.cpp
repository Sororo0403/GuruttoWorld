#include <Engine/Audio/AudioDecoder.h>
#include <Engine/Core/Log.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>
#include <format>
#include <memory>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")

namespace
{
    using Microsoft::WRL::ComPtr;
    constexpr size_t MaxDecodedBytes = 64 * 1024 * 1024;

    bool Check(HRESULT result, const char* operation)
    {
        if (SUCCEEDED(result)) return true;
        Engine::Log::Error(std::format("{} failed: 0x{:08X}", operation, static_cast<unsigned long>(result)));
        return false;
    }

    bool IsSupportedPcmFormat(const WAVEFORMATEX& format)
    {
        return !(format.wFormatTag != WAVE_FORMAT_PCM || format.cbSize != 0 ||
            (format.nChannels != 1 && format.nChannels != 2) || format.nSamplesPerSec < 8000 || format.nSamplesPerSec > 192000 ||
            (format.wBitsPerSample != 8 && format.wBitsPerSample != 16 && format.wBitsPerSample != 24 && format.wBitsPerSample != 32) ||
            format.nBlockAlign != format.nChannels * (format.wBitsPerSample / 8) ||
            format.nAvgBytesPerSec != format.nSamplesPerSec * format.nBlockAlign);
    }

    bool NormalizePcmFormat(const WAVEFORMATEX* allocated, UINT32 size, WAVEFORMATEX& format)
    {
        if (format.wFormatTag == WAVE_FORMAT_EXTENSIBLE)
        {
            if (size < sizeof(WAVEFORMATEXTENSIBLE) ||
                format.cbSize < sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX) ||
                size < sizeof(WAVEFORMATEX) + format.cbSize)
            {
                Engine::Log::Error("Incomplete decoded extensible PCM format.");
                return false;
            }
            const auto& extended = *reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(allocated);
            const DWORD defaultMask = format.nChannels == 1 ? SPEAKER_FRONT_CENTER : SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
            if (!IsEqualGUID(extended.SubFormat, MFAudioFormat_PCM) ||
                extended.Samples.wValidBitsPerSample == 0 || extended.Samples.wValidBitsPerSample > format.wBitsPerSample ||
                (extended.dwChannelMask != 0 && extended.dwChannelMask != defaultMask))
            {
                Engine::Log::Error("Unsupported decoded PCM subtype, valid bits or channel layout.");
                return false;
            }
            // PCM の有効ビットは左詰めなので、格納幅の PCM として同じ波形を再生できます。
            // 通常のモノラル／ステレオ配置だけを受理し、拡張情報を安全に正規化します。
            format.wFormatTag = WAVE_FORMAT_PCM;
            format.cbSize = 0;
        }
        if (!IsSupportedPcmFormat(format))
        {
            Engine::Log::Error("Unsupported decoded PCM format (mono/stereo required).");
            return false;
        }
        return true;
    }

    bool ConfigureReader(IMFSourceReader* reader, WAVEFORMATEX& format)
    {
        ComPtr<IMFMediaType> requested;
        ComPtr<IMFMediaType> actual;
        if (!Check(reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS), FALSE), "Deselect media streams") ||
            !Check(reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), TRUE), "Select audio stream") ||
            !Check(MFCreateMediaType(&requested), "Create PCM type") ||
            !Check(requested->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio), "Set audio type") ||
            !Check(requested->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM), "Set PCM subtype") ||
            !Check(reader->SetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), nullptr, requested.Get()), "Configure audio decoder") ||
            !Check(reader->GetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), &actual), "Get PCM type")) return false;
        WAVEFORMATEX* allocated = nullptr;
        UINT32 size = 0;
        const HRESULT result = MFCreateWaveFormatExFromMFMediaType(actual.Get(), &allocated, &size);
        const std::unique_ptr<WAVEFORMATEX, decltype(&CoTaskMemFree)> owner(allocated, CoTaskMemFree);
        if (!Check(result, "Convert PCM format") || allocated == nullptr || size < sizeof(WAVEFORMATEX)) return false;
        format = *allocated;
        return NormalizePcmFormat(allocated, size, format);
    }

    bool AppendSample(IMFSample* sample, std::vector<unsigned char>& samples)
    {
        ComPtr<IMFMediaBuffer> buffer;
        if (!Check(sample->ConvertToContiguousBuffer(&buffer), "Get audio buffer")) return false;
        BYTE* data = nullptr;
        DWORD length = 0;
        if (!Check(buffer->Lock(&data, nullptr, &length), "Lock audio buffer")) return false;
        struct Unlock
        {
            IMFMediaBuffer* buffer;
            ~Unlock() { buffer->Unlock(); }
        } unlock{ buffer.Get() };
        if (length > MaxDecodedBytes - samples.size())
        {
            Engine::Log::Error("Decoded audio exceeds 64 MiB.");
            return false;
        }
        if (length != 0) samples.insert(samples.end(), data, data + length);
        return true;
    }
}

namespace Engine
{
    bool DecodeAudioFile(const std::filesystem::path& path, WaveData& wave)
    {
        std::error_code error;
        if (path.empty() || !std::filesystem::is_regular_file(path, error) || error)
        {
            Log::Error("Audio file does not exist or is not a regular file.");
            return false;
        }
        ComPtr<IMFSourceReader> reader;
        if (!Check(MFCreateSourceReaderFromURL(path.c_str(), nullptr, &reader), "Open audio source")) return false;
        WaveData decoded;
        if (!ConfigureReader(reader.Get(), decoded.format)) return false;
        for (;;)
        {
            DWORD flags = 0;
            ComPtr<IMFSample> sample;
            if (!Check(reader->ReadSample(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), 0, nullptr, &flags, nullptr, &sample), "Decode audio sample")) return false;
            if ((flags & (MF_SOURCE_READERF_ERROR | MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED)) != 0)
            {
                Log::Error("Audio decoding failed or PCM format changed during decoding.");
                return false;
            }
            if (sample && !AppendSample(sample.Get(), decoded.samples)) return false;
            if ((flags & MF_SOURCE_READERF_ENDOFSTREAM) != 0) break;
        }
        if (decoded.samples.empty() || decoded.samples.size() % decoded.format.nBlockAlign != 0)
        {
            Log::Error("Decoded audio is empty or contains an incomplete PCM frame.");
            return false;
        }
        wave = std::move(decoded);
        return true;
    }
}
