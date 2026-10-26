#include <Engine/Audio/WaveData.h>
#include <Engine/Audio/AudioDecoder.h>
#include <mfapi.h>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace
{
    const std::filesystem::path Source = "tests/fixtures/Player/Assets/Audio/Sample.wav";

    void Require(bool condition, const char* message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    void Compare(const Engine::WaveData& first, const Engine::WaveData& second)
    {
        Require(first.samples == second.samples && first.format.wFormatTag == second.format.wFormatTag &&
                    first.format.nChannels == second.format.nChannels &&
                    first.format.nSamplesPerSec == second.format.nSamplesPerSec &&
                    first.format.wBitsPerSample == second.format.wBitsPerSample &&
                    first.format.nBlockAlign == second.format.nBlockAlign &&
                    first.format.nAvgBytesPerSec == second.format.nAvgBytesPerSec,
                "compatibility loader matches shared decoder");
    }

    void ValidateSharedSession(const Engine::WaveData& standalone)
    {
        Require(SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)), "initialize caller COM");
        Require(SUCCEEDED(MFStartup(MF_VERSION)), "initialize caller MF");
        Engine::WaveData wave, decoded;
        Require(Engine::LoadWaveFile(Source, wave), "load inside caller session");
        Require(Engine::DecodeAudioFile(Source, decoded), "caller session survives compatibility loader");
        Compare(standalone, wave);
        Compare(decoded, wave);
        MFShutdown();
        CoUninitialize();
    }

    void ValidateStaSession(const Engine::WaveData& standalone)
    {
        bool succeeded = false;
        std::thread worker(
            [&]
            {
                const HRESULT result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
                if (SUCCEEDED(result))
                {
                    Engine::WaveData wave;
                    succeeded = Engine::LoadWaveFile(Source, wave) && wave.samples == standalone.samples;
                    CoUninitialize();
                }
            });
        worker.join();
        Require(succeeded, "compatibility loader accepts caller STA apartment");
    }
}

int main()
{
    try
    {
        Engine::WaveData wave;
        Require(Engine::LoadWaveFile(Source, wave), "load without COM or MF initialization");
        Require(!wave.samples.empty(), "standalone load has PCM samples");
        ValidateSharedSession(wave);
        ValidateStaSession(wave);
        Engine::WaveData repeated;
        Require(Engine::LoadWaveFile(Source, repeated), "load after caller MF shutdown");
        Compare(wave, repeated);

        const auto directory = std::filesystem::path("generated/tests/wave-loader") /
                               (std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
        std::filesystem::create_directories(directory);
        const auto invalid = directory / "invalid.wav";
        {
            std::ofstream output(invalid, std::ios::binary);
            output << "not a WAV file";
        }
        Require(!Engine::LoadWaveFile(invalid, repeated), "invalid WAV is rejected");
        Compare(wave, repeated);
        Require(!Engine::LoadWaveFile(directory / "missing.wav", repeated), "missing WAV is rejected");
        Compare(wave, repeated);
        std::filesystem::resize_file(invalid, 64 * 1024 * 1024 + 1);
        Require(!Engine::LoadWaveFile(invalid, repeated), "oversized WAV is rejected");
        Compare(wave, repeated);
        std::filesystem::remove(invalid);
        std::filesystem::remove(directory);
        std::cout << "PASS: shared WAV decoder, standalone/caller/STA lifecycle and failure preservation\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
