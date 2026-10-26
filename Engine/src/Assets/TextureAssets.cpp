#include <Engine/Assets/TextureAssets.h>
#include <Engine/Assets/AssetDatabase.h>
#include <Engine/Core/Log.h>
#include <Engine/Core/Profiler.h>
#include "AssetImportCache.h"
#include "CpuAssetBinary.h"
#include <wincodec.h>
#include <wrl/client.h>
#include <format>
#include <map>
#include <mutex>
#include <thread>
#include <limits>
#include <array>
#include <optional>
#include <algorithm>
#pragma comment(lib, "Windowscodecs.lib")
#pragma comment(lib, "Ole32.lib")
namespace
{
    using Microsoft::WRL::ComPtr;
    using Engine::Log;
    struct PreparedTexture : Engine::TextureAssetData
    {
        std::vector<unsigned char> source;
        size_t bytes = 0;
        bool sourceInBase = false;
    };

    std::optional<std::string> PixelDigest(const std::vector<unsigned char>& pixels)
    {
        try
        {
            return Engine::ArtifactHash::Bytes({reinterpret_cast<const char*>(pixels.data()), pixels.size()});
        }
        catch (...)
        {
            return {};
        }
    }

    std::shared_ptr<const PreparedTexture> PrepareTexture(Engine::TextureLevel source,
                                                          const Engine::TextureImportSettings& settings)
    {
        settings.Validate();
        constexpr size_t Budget = 192 * 1024 * 1024;
        using Key = std::string;
        struct Entry
        {
            std::shared_ptr<const PreparedTexture> texture;
            uint64_t used = 0;
        };
        static std::mutex mutex;
        static std::map<Key, Entry> entries;
        static size_t bytes = 0;
        static uint64_t sequence = 0;
        const bool transformed = settings.mipmaps || std::max(source.width, source.height) > settings.maxSize ||
                                 Engine::TextureImport::Compressed(settings, source);
        const auto digest = transformed && source.pixels.size() <= Budget ? PixelDigest(source.pixels) : std::nullopt;
        Engine::ArtifactDescriptor descriptor;
        descriptor.kind = "texture-mips";
        descriptor.assetId = "decoded-pixels";
        descriptor.sourceRevision = digest.value_or("uncached");
        descriptor.importerVersion = "DirectXTex/4feb3e11a020f35b796fc769a74216a555d4f5ef/WP1-v1";
        descriptor.settings =
            Engine::Json{{"width", source.width}, {"height", source.height},     {"mipmaps", settings.mipmaps},
                         {"srgb", settings.srgb}, {"maxSize", settings.maxSize}, {"compression", settings.compression}}
                .dump();
        descriptor.platform = "windows-x64";
        const auto key = digest ? descriptor.Key() : std::string{};
        if (digest)
        {
            std::lock_guard lock(mutex);
            const auto found = entries.find(key);
            // ダイジェストだけで同一と判断せず、元の全画素も照合します。
            if (found != entries.end() &&
                (found->second.texture->sourceInBase ? found->second.texture->levels.front().pixels
                                                     : found->second.texture->source) == source.pixels)
            {
                found->second.used = ++sequence;
                Engine::CpuProfiler::Add("Texture mip cache hit", 0);
                return found->second.texture;
            }
        }
        auto prepared = std::make_shared<PreparedTexture>();
        // 基底ミップが入力のままなら、その画素を照合にも使って二重保持を避けます。
        prepared->sourceInBase = std::max(source.width, source.height) <= settings.maxSize &&
                                 !Engine::TextureImport::Compressed(settings, source);
        if (digest && !prepared->sourceInBase)
            prepared->source = source.pixels;
        {
            Engine::CpuScope build("Texture mip build");
            prepared->levels = Engine::TextureImport::Build(std::move(source), settings);
        }
        prepared->compressed = Engine::TextureImport::Compressed(settings, prepared->levels.front());
        prepared->bytes = prepared->source.size();
        for (const auto& level : prepared->levels)
            prepared->bytes += level.pixels.size();
        if (digest && prepared->bytes <= Budget)
        {
            std::lock_guard lock(mutex);
            if (const auto old = entries.find(key); old != entries.end())
            {
                bytes -= old->second.texture->bytes;
                entries.erase(old);
            }
            while (!entries.empty() && (entries.size() >= 128 || bytes + prepared->bytes > Budget))
            {
                const auto oldest = std::min_element(entries.begin(), entries.end(),
                                                     [](const auto& a, const auto& b)
                                                     {
                                                         return a.second.used < b.second.used;
                                                     });
                bytes -= oldest->second.texture->bytes;
                entries.erase(oldest);
            }
            bytes += prepared->bytes;
            entries.emplace(key, Entry{prepared, ++sequence});
        }
        else
            std::vector<unsigned char>().swap(prepared->source);
        return prepared;
    }

    bool Check(HRESULT result, const char* operation)
    {
        if (FAILED(result))
        {
            Engine::Log::Error(std::format("{} failed: 0x{:08X}", operation, static_cast<unsigned long>(result)));
            return false;
        }
        return true;
    }

    struct ComApartment
    {
        HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        ~ComApartment()
        {
            if (SUCCEEDED(result))
            {
                CoUninitialize();
            }
        }
    };
    bool DecodeImage(const std::filesystem::path& path, Engine::TextureLevel& image)
    {
        Engine::CpuScope timing("Texture decode");
        ComApartment apartment;
        if (apartment.result != RPC_E_CHANGED_MODE && !Check(apartment.result, "Initialize WIC COM apartment"))
        {
            return false;
        }
        ComPtr<IWICImagingFactory> factory;
        ComPtr<IWICBitmapDecoder> decoder;
        ComPtr<IWICBitmapFrameDecode> frame;
        ComPtr<IWICFormatConverter> converter;
        if (!Check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)),
                   "Create WIC factory") ||
            !Check(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad,
                                                      &decoder),
                   "Open texture image") ||
            !Check(decoder->GetFrame(0, &frame), "Decode texture frame") ||
            !Check(frame->GetSize(&image.width, &image.height), "Get texture size"))
        {
            return false;
        }
        if (image.width == 0 || image.height == 0 || image.width > 16384 || image.height > 16384)
        {
            Log::Error("Unsupported texture dimensions.");
            return false;
        }
        const UINT64 size = static_cast<UINT64>(image.width) * image.height * 4;
        if (size > (std::numeric_limits<UINT>::max)())
        {
            Log::Error("Texture pixel data is too large.");
            return false;
        }
        if (!Check(factory->CreateFormatConverter(&converter), "Create WIC converter") ||
            !Check(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr,
                                         0.0, WICBitmapPaletteTypeCustom),
                   "Convert texture to RGBA8"))
        {
            return false;
        }
        image.pixels.resize(static_cast<size_t>(size));
        return Check(converter->CopyPixels(nullptr, image.width * 4, static_cast<UINT>(size), image.pixels.data()),
                     "Read texture pixels");
    }
    std::string Encode(const Engine::TextureAssetData& data)
    {
        Engine::AssetBinary::Writer output;
        output.Pod(std::uint32_t{1});
        output.Pod(static_cast<std::uint8_t>(data.compressed));
        output.Count(data.levels.size());
        for (const auto& level : data.levels)
        {
            output.Pod(level.width);
            output.Pod(level.height);
            output.Array(level.pixels);
        }
        return std::move(output.bytes);
    }
    std::shared_ptr<const Engine::TextureAssetData> Decode(std::string_view bytes)
    {
        Engine::AssetBinary::Reader input(bytes);
        if (input.Pod<std::uint32_t>() != 1)
            throw std::runtime_error("Unsupported CPU texture artifact");
        auto data = std::make_shared<Engine::TextureAssetData>();
        const auto compressed = input.Pod<std::uint8_t>();
        if (compressed > 1)
            throw std::runtime_error("Invalid CPU texture format");
        data->compressed = compressed != 0;
        const auto count = input.Count();
        if (!count || count > 15)
            throw std::runtime_error("Invalid CPU texture mip count");
        unsigned int width = 0, height = 0;
        for (std::uint32_t index = 0; index < count; ++index)
        {
            Engine::TextureLevel level;
            level.width = input.Pod<unsigned int>();
            level.height = input.Pod<unsigned int>();
            level.pixels = input.Array<unsigned char>();
            if (!level.width || !level.height || level.width > 16384 || level.height > 16384 ||
                (index && (level.width != std::max(1u, width / 2) || level.height != std::max(1u, height / 2))))
                throw std::runtime_error("Invalid CPU texture mip dimensions");
            const auto expected = data->compressed ? size_t((level.width + 3) / 4) * ((level.height + 3) / 4) * 16
                                                   : size_t(level.width) * level.height * 4;
            if (expected != level.pixels.size())
                throw std::runtime_error("Invalid CPU texture mip payload");
            width = level.width;
            height = level.height;
            data->levels.push_back(std::move(level));
        }
        input.End();
        return data;
    }
    std::string WicIdentity()
    {
        static const std::string identity = []
        {
            const auto module = LoadLibraryExW(L"windowscodecs.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
            if (!module)
                throw std::runtime_error("Cannot identify WIC importer");
            struct Module
            {
                HMODULE value;
                ~Module()
                {
                    FreeLibrary(value);
                }
            } owned{module};
            std::array<wchar_t, 32768> path{};
            const auto length = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
            if (!length || length >= path.size())
                throw std::runtime_error("Cannot identify WIC importer path");
            return Engine::ArtifactHash::File(path.data());
        }();
        return identity;
    }
    std::mutex textureMutex;
    std::map<std::pair<std::string, std::filesystem::path>, std::weak_ptr<const Engine::TextureAssetData>>
        textureLeases;

}

namespace Engine
{
    bool TextureAssetRequest::Ready() const
    {
        return request_ && request_->ticket.Ready();
    }
    std::shared_ptr<const TextureAssetData> TextureAssetRequest::Publish(std::string& error) const
    {
        try
        {
            const auto bytes = request_ ? request_->Publish(error) : nullptr;
            if (!bytes)
                return {};
            std::lock_guard lock(textureMutex);
            auto& lease = textureLeases[{bytes->key, request_->source}];
            if (auto data = lease.lock())
                return data;
            auto data = Decode(bytes->bytes);
            lease = data;
            std::erase_if(textureLeases,
                          [](const auto& item)
                          {
                              return item.second.expired();
                          });
            return data;
        }
        catch (const std::exception& failure)
        {
            error = failure.what();
            return {};
        }
    }
    TextureAssetRequest TextureAssets::Request(const std::filesystem::path& path)
    {
        TextureAssetRequest result;
        const auto settings = std::make_shared<TextureImportSettings>();
        result.request_ = RequestAssetImport(
            path, "texture-mips",
            [settings](const std::filesystem::path& source)
            {
                TextureLevel image;
                if (!DecodeImage(source, image))
                    throw std::runtime_error("Texture image import failed: " + AssetPath::Text(source));
                return Encode(*PrepareTexture(std::move(image), *settings));
            },
            [settings](const std::filesystem::path& source, ArtifactDescriptor& descriptor, ArtifactSources& sources)
            {
                auto metadata = source;
                metadata += L".meta";
                sources = ArtifactSources::Capture({{"source", source}, {"metadata", metadata}});
                sources.ProtectOnly("metadata");
                const auto values = AssetDatabase::Read(source);
                *settings = values.texture;
                settings->Validate();
                if (!sources.Inputs().front().revision || !sources.Unchanged())
                    throw std::runtime_error("Texture source changed during capture");
                descriptor = {"texture-mips",
                              values.id,
                              *sources.Inputs().front().revision,
                              "WIC-RGBA8/DirectXTex-4feb3e11a020f35b796fc769a74216a555d4f5ef/WP1-v1",
                              Json{{"mipmaps", settings->mipmaps},
                                   {"srgb", settings->srgb},
                                   {"maxSize", settings->maxSize},
                                   {"compression", settings->compression}}
                                  .dump(),
                              "windows-x64",
                              WicIdentity(),
                              {}};
            });
        return result;
    }
    std::shared_ptr<const TextureAssetData> TextureAssets::Load(const std::filesystem::path& path, std::string& error)
    {
        try
        {
            const auto request = Request(path);
            while (!request.Ready())
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            return request.Publish(error);
        }
        catch (const std::exception& failure)
        {
            error = failure.what();
            return {};
        }
    }
    std::shared_ptr<const TextureAssetData> TextureAssets::PreparePixels(TextureLevel source,
                                                                         const TextureImportSettings& settings)
    {
        return PrepareTexture(std::move(source), settings);
    }
    bool TextureAssets::Cook(const std::filesystem::path& path, const std::filesystem::path& directory,
                             std::string& error)
    {
        try
        {
            return Request(path).request_->Cook(directory, error);
        }
        catch (const std::exception& failure)
        {
            error = failure.what();
            return false;
        }
    }
}
