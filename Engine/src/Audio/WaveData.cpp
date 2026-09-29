#include <Engine/Audio/WaveData.h>
#include <Engine/Core/Log.h>

#include <cstdint>
#include <cstring>
#include <fstream>
#include <utility>

namespace
{
    std::uint32_t Read32(const unsigned char* bytes)
    {
        return bytes[0] | (std::uint32_t(bytes[1]) << 8) | (std::uint32_t(bytes[2]) << 16) | (std::uint32_t(bytes[3]) << 24);
    }

    WORD Read16(const unsigned char* bytes)
    {
        return static_cast<WORD>(bytes[0] | (unsigned(bytes[1]) << 8));
    }

    bool Reject()
    {
        Engine::Log::Error("Invalid or unsupported WAV file (PCM mono/stereo, 8/16/24/32 bit required).");
        return false;
    }
}

namespace Engine
{
    bool LoadWaveFile(const std::filesystem::path& path, WaveData& wave)
    {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file)
        {
            Log::Error("Cannot open WAV file.");
            return false;
        }
        const auto length = file.tellg();
        if (length < 12 || length > 64 * 1024 * 1024)
        {
            return Reject();
        }
        std::vector<unsigned char> bytes(static_cast<size_t>(length));
        file.seekg(0);
        if (!file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())) ||
            std::memcmp(bytes.data(), "RIFF", 4) != 0 || std::memcmp(bytes.data() + 8, "WAVE", 4) != 0)
        {
            return Reject();
        }
        const std::uint64_t end = std::uint64_t(Read32(bytes.data() + 4)) + 8;
        if (end < 12 || end > bytes.size())
        {
            return Reject();
        }
        WaveData loaded;
        bool hasFormat = false, hasData = false;
        for (std::uint64_t offset = 12; offset < end;)
        {
            if (end - offset < 8) return Reject();
            const auto* chunk = bytes.data() + static_cast<size_t>(offset);
            const std::uint32_t size = Read32(chunk + 4);
            const std::uint64_t next = offset + 8 + size + (size & 1);
            if (next > end) return Reject();
            const auto* data = chunk + 8;
            if (std::memcmp(chunk, "fmt ", 4) == 0)
            {
                if (hasFormat || size < 16) return Reject();
                auto& f = loaded.format;
                f.wFormatTag = Read16(data);
                f.nChannels = Read16(data + 2);
                f.nSamplesPerSec = Read32(data + 4);
                f.nAvgBytesPerSec = Read32(data + 8);
                f.nBlockAlign = Read16(data + 12);
                f.wBitsPerSample = Read16(data + 14);
                if (f.wFormatTag != WAVE_FORMAT_PCM || (f.nChannels != 1 && f.nChannels != 2) ||
                    (f.wBitsPerSample != 8 && f.wBitsPerSample != 16 && f.wBitsPerSample != 24 && f.wBitsPerSample != 32) ||
                    f.nSamplesPerSec < 8000 || f.nSamplesPerSec > 192000 ||
                    f.nBlockAlign != f.nChannels * (f.wBitsPerSample / 8) ||
                    f.nAvgBytesPerSec != f.nSamplesPerSec * f.nBlockAlign)
                {
                    return Reject();
                }
                hasFormat = true;
            }
            else if (std::memcmp(chunk, "data", 4) == 0)
            {
                if (hasData || size == 0) return Reject();
                loaded.samples.assign(data, data + size);
                hasData = true;
            }
            offset = next;
        }
        if (!hasFormat || !hasData || loaded.samples.size() % loaded.format.nBlockAlign != 0)
        {
            return Reject();
        }
        wave = std::move(loaded);
        return true;
    }
}
