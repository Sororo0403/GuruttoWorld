#include <SceneRuntime/Rendering/MaterialTextureCache.h>

namespace SceneRuntime
{
    auto MaterialTextureCache::Load(ID3D12Device* device, ID3D12CommandQueue* queue, const std::filesystem::path& path)
        -> std::shared_ptr<const Engine::Texture2D>
    {
        using Key = std::tuple<ID3D12Device*, ID3D12CommandQueue*, std::filesystem::path>;
        struct Entry
        {
            Engine::AssetSourceStamp source;
            std::weak_ptr<const Engine::Texture2D> texture;
        };
        static std::mutex mutex;
        static std::map<Key, Entry> entries;
        const auto canonical = std::filesystem::canonical(path);
        const auto source = Engine::AssetSourceStamp::Capture(canonical);
        if (!source)
            throw std::runtime_error("Cannot read material texture source");
        std::lock_guard lock(mutex);
        std::erase_if(entries,
                      [](const auto& pair)
                      {
                          return pair.second.texture.expired();
                      });
        const Key key{device, queue, canonical};
        if (const auto found = entries.find(key); found != entries.end() && found->second.source == *source)
        {
            if (auto texture = found->second.texture.lock())
                return texture;
        }
        auto texture = std::make_shared<Engine::Texture2D>();
        if (!texture->Initialize(device, queue, canonical))
            throw std::runtime_error("Cannot load material texture");
        if (Engine::AssetSourceStamp::Capture(canonical) != source)
            throw std::runtime_error("Material texture source changed during import");
        entries.insert_or_assign(key, Entry{*source, texture});
        return texture;
    }
}
