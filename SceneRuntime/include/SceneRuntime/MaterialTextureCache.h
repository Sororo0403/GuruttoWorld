#pragma once
#include <Engine/Graphics/Resources/Texture2D.h>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <tuple>

namespace SceneRuntime
{
    class MaterialTextureCache final
    {
    public:
        /// <summary>同じ描画所有者の画像を共有し、画像とメタデータの変更時だけ転送し直します。</summary>
        static std::shared_ptr<const Engine::Texture2D> Load(ID3D12Device* device, ID3D12CommandQueue* queue,
                                                             const std::filesystem::path& path)
        {
            using Key = std::tuple<ID3D12Device*, ID3D12CommandQueue*, std::filesystem::path>;
            struct Entry
            {
                std::filesystem::file_time_type modified, metadataModified;
                std::uintmax_t size = 0;
                std::weak_ptr<const Engine::Texture2D> texture;
            };
            static std::mutex mutex;
            static std::map<Key, Entry> entries;
            const auto canonical = std::filesystem::canonical(path);
            const auto modified = std::filesystem::last_write_time(canonical);
            const auto size = std::filesystem::file_size(canonical);
            auto metadata = canonical;
            metadata += ".meta";
            const auto metadataModified = std::filesystem::exists(metadata) ? std::filesystem::last_write_time(metadata)
                                                                            : std::filesystem::file_time_type{};
            std::lock_guard lock(mutex);
            std::erase_if(entries,
                          [](const auto& pair)
                          {
                              return pair.second.texture.expired();
                          });
            const Key key{device, queue, canonical};
            if (const auto found = entries.find(key); found != entries.end() && found->second.modified == modified &&
                                                      found->second.size == size &&
                                                      found->second.metadataModified == metadataModified)
            {
                if (auto texture = found->second.texture.lock())
                    return texture;
            }
            auto texture = std::make_shared<Engine::Texture2D>();
            if (!texture->Initialize(device, queue, canonical))
                throw std::runtime_error("Cannot load material texture");
            entries.insert_or_assign(key, Entry{modified, metadataModified, size, texture});
            return texture;
        }
    };
}
