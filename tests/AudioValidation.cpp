#include <Engine/Audio/AudioSystem.h>
#include <Engine/Audio/AudioDecoder.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <cstring>
#include <thread>
#include <chrono>

namespace
{
    using Microsoft::WRL::ComPtr;
    void Check(bool condition, const char* message)
    {
        if (!condition) throw std::runtime_error(message);
    }
    void Hr(HRESULT result)
    {
        if (FAILED(result)) throw std::runtime_error("Media Foundation test encoder failed: " + std::to_string(result));
    }
    std::vector<unsigned char> WritePcmWave(const std::filesystem::path& path, WORD bits, WORD channels,
        bool extensible, WORD validBits, DWORD channelMask)
    {
        constexpr DWORD Frames = 4800;
        const WORD block = static_cast<WORD>(channels * bits / 8);
        const DWORD bytes = Frames * block;
        const DWORD formatSize = extensible ? 40 : 16;
        std::vector<unsigned char> samples(bytes, 0);
        for (DWORD frame = 0; frame < Frames; ++frame)
        {
            for (WORD channel = 0; channel < channels; ++channel)
                samples[frame * block + (channel + 1) * (bits / 8) - 1] = (frame + channel) % 2 == 0 ? 0x20 : 0xE0;
        }
        std::ofstream file(path, std::ios::binary);
        auto put = [&](auto value) { file.write(reinterpret_cast<const char*>(&value), sizeof(value)); };
        file.write("RIFF", 4); put(DWORD(20 + formatSize + bytes)); file.write("WAVEfmt ", 8); put(formatSize);
        put(WORD(extensible ? WAVE_FORMAT_EXTENSIBLE : WAVE_FORMAT_PCM)); put(channels);
        put(DWORD(48000)); put(DWORD(48000 * block)); put(block); put(bits);
        if (extensible)
        {
            put(WORD(22)); put(validBits); put(channelMask); put(MFAudioFormat_PCM);
        }
        file.write("data", 4); put(bytes);
        file.write(reinterpret_cast<const char*>(samples.data()), samples.size());
        file.close();
        Check(bool(file), "write PCM fixture");
        return samples;
    }

    void ValidateExtensiblePcmFormats(const std::filesystem::path& folder)
    {
        for (DWORD mask : {DWORD(0), DWORD(SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT)})
        {
            const auto path = folder / ("24-in-32-" + std::to_string(mask) + ".wav");
            const auto expected = WritePcmWave(path, 32, 2, true, 24, mask);
            Engine::WaveData decoded;
            Check(Engine::DecodeAudioFile(path, decoded) && decoded.format.wBitsPerSample == 32 &&
                decoded.samples == expected, "left-aligned valid bits preserve samples");
        }
        const auto unsupported = folder / "nonstandard-layout.wav";
        WritePcmWave(unsupported, 24, 2, true, 24, SPEAKER_BACK_LEFT | SPEAKER_BACK_RIGHT);
        Engine::WaveData preserved;
        preserved.samples = {1, 2, 3};
        Check(!Engine::DecodeAudioFile(unsupported, preserved) && preserved.samples == std::vector<unsigned char>({1, 2, 3}),
            "unsupported channel layout rejected without losing output");
    }

    void ValidatePcmFormats(Engine::AudioSystem& audio, const std::filesystem::path& folder)
    {
        for (WORD channels : {WORD(1), WORD(2)})
        {
            for (WORD bits : {WORD(8), WORD(16), WORD(24), WORD(32)})
            {
                for (bool extensible : {false, true})
                {
                    const auto path = folder / (std::to_string(channels) + "ch-" + std::to_string(bits) +
                        (extensible ? "-ext.wav" : "-pcm.wav"));
                    const DWORD mask = channels == 1 ? SPEAKER_FRONT_CENTER : SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
                    const auto expected = WritePcmWave(path, bits, channels, extensible, bits, mask);
                    Engine::WaveData decoded;
                    Check(Engine::DecodeAudioFile(path, decoded), "decode mono/stereo PCM and extensible WAV");
                    Check(decoded.format.wFormatTag == WAVE_FORMAT_PCM && decoded.format.cbSize == 0 &&
                        decoded.format.nChannels == channels && decoded.format.wBitsPerSample == bits &&
                        decoded.samples == expected, "normalized PCM preserves format and sample bytes");
                    const auto handle = audio.Load(path);
                    Check(handle != 0 && audio.SetVolume(handle, 0) && audio.Play(handle), "play normalized PCM silently");
                    audio.Unload(handle);
                }
            }
        }
        ValidateExtensiblePcmFormats(folder);
    }

    void EncodeAac(const std::filesystem::path& path)
    {
        Engine::WaveData wave;
        Check(Engine::LoadWaveFile("App/Assets/Audio/Sample.wav", wave), "source WAV");
        ComPtr<IMFSinkWriter> writer;
        ComPtr<IMFMediaType> compressed, pcm;
        Hr(MFCreateSinkWriterFromURL(path.c_str(), nullptr, nullptr, &writer));
        Hr(MFCreateMediaType(&compressed));
        Hr(compressed->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio));
        Hr(compressed->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC));
        Hr(compressed->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, wave.format.nChannels));
        Hr(compressed->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, wave.format.nSamplesPerSec));
        Hr(compressed->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16));
        Hr(compressed->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 12000));
        DWORD stream = 0;
        Hr(writer->AddStream(compressed.Get(), &stream));
        Hr(MFCreateMediaType(&pcm));
        Hr(MFInitMediaTypeFromWaveFormatEx(pcm.Get(), &wave.format, sizeof(wave.format)));
        Hr(writer->SetInputMediaType(stream, pcm.Get(), nullptr));
        Hr(writer->BeginWriting());
        ComPtr<IMFSample> sample;
        ComPtr<IMFMediaBuffer> buffer;
        const DWORD size = static_cast<DWORD>(wave.samples.size());
        Hr(MFCreateSample(&sample));
        Hr(MFCreateMemoryBuffer(size, &buffer));
        BYTE* data = nullptr;
        Hr(buffer->Lock(&data, nullptr, nullptr));
        std::memcpy(data, wave.samples.data(), size);
        Hr(buffer->Unlock());
        Hr(buffer->SetCurrentLength(size));
        Hr(sample->AddBuffer(buffer.Get()));
        Hr(sample->SetSampleTime(0));
        Hr(sample->SetSampleDuration(static_cast<LONGLONG>(size) * 10000000 / wave.format.nAvgBytesPerSec));
        Hr(writer->WriteSample(stream, sample.Get()));
        Hr(writer->Finalize());
    }
}
int main()
{
    try
    {
        Engine::AudioSystem audio;
        Check(audio.Initialize(), "initialize");
        Check(!audio.Initialize(), "double initialize");
        const auto folder = std::filesystem::path("generated/tests/audio") / std::to_string(GetTickCount64());
        std::filesystem::create_directories(folder);
        ValidatePcmFormats(audio, folder);
        const auto compressed = folder / "sample.m4a";
        EncodeAac(compressed);
        Engine::WaveData decoded;
        Check(Engine::DecodeAudioFile(compressed, decoded), "decode AAC");
        Check(decoded.format.wFormatTag == WAVE_FORMAT_PCM && decoded.format.nSamplesPerSec == 48000 && !decoded.samples.empty(), "decoded PCM");
        const auto before = decoded.samples;
        const auto invalid = folder / "invalid.mp3";
        { std::ofstream file(invalid); file << "not audio"; }
        Check(!Engine::DecodeAudioFile(invalid, decoded) && decoded.samples == before, "failed decode preserves output");
        Check(audio.Load(invalid) == 0 && audio.Load(folder / "missing.mp3") == 0, "invalid files rejected");
        const auto wav = audio.Load("App/Assets/Audio/Sample.wav");
        const auto aac = audio.Load(compressed);
        Check(wav != 0 && aac != 0 && wav != aac, "load WAV and AAC");
        Check(audio.GetVolume(wav) == 1.0f && audio.GetVolume(0) == 0.0f, "initial and invalid volume");
        Check(audio.SetVolume(wav, 0.25f) && audio.GetVolume(wav) == 0.25f, "volume readback");
        Check(!audio.SetVolume(wav, -1.0f) && audio.GetVolume(wav) == 0.25f, "invalid volume preserves state");
        std::filesystem::rename(compressed, folder / "moved.m4a");
        for (const auto handle : { wav, aac })
        {
            Check(audio.SetVolume(handle, 0), "mute test playback");
            Check(audio.GetVolume(handle) == 0, "UI reads muted volume from audio");
            Check(audio.Play(handle), "play cached audio");
            Check(audio.IsPlaying(handle), "playing state");
            Check(audio.Pause(handle,true), "pause finite voice");
            std::this_thread::sleep_for(std::chrono::milliseconds(900));
            Check(audio.IsPlaying(handle), "paused finite buffer does not advance");
            Check(audio.Pause(handle,false), "resume finite voice from retained buffer");
            std::this_thread::sleep_for(std::chrono::milliseconds(900));
            Check(!audio.IsPlaying(handle), "playback completes");
            Check(audio.Play(handle, true), "loop replay without source file");
            std::this_thread::sleep_for(std::chrono::milliseconds(700));
            Check(audio.IsPlaying(handle), "loop remains active");
            Check(audio.Pause(handle,true), "pause keeps queued audio");
            std::this_thread::sleep_for(std::chrono::milliseconds(700));
            Check(audio.IsPlaying(handle), "paused loop retains buffer");
            Check(audio.Pause(handle,false), "resume voice");
            audio.Stop(handle);
            Check(!audio.IsPlaying(handle), "stop");
            audio.Unload(handle);
            Check(audio.GetVolume(handle) == 0, "unloaded volume is unavailable");
            Check(!audio.Play(handle), "unloaded handle rejected");
        }
        audio.Shutdown();
        audio.Shutdown();
        Check(audio.Initialize(), "reinitialize Media Foundation and audio");
        const auto reloaded = audio.Load(folder / "moved.m4a");
        Check(reloaded != 0, "decode after reinitialize");
        Check(audio.SetVolume(reloaded, 0.25f) && audio.GetVolume(reloaded) == 0.25f,
            "recreated sound exposes fresh scene volume rather than previous mute");
        std::cout << "PASS: PCM/extensible WAV 8/16/24/32-bit, valid bits, channel layouts, AAC, playback and lifecycle\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
