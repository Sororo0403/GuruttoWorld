#include <Engine/Graphics/Resources/TextureManager.h>
#include <Engine/Core/Log.h>

namespace Engine
{
    bool TextureManager::Initialize(ID3D12Device* device, ID3D12CommandQueue* queue)
    {
        if (device_ || device == nullptr || queue == nullptr || queue->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT)
        {
            return false;
        }
        Microsoft::WRL::ComPtr<ID3D12Device> queueDevice;
        if (FAILED(queue->GetDevice(IID_PPV_ARGS(&queueDevice))) || queueDevice.Get() != device)
        {
            Log::Error("Texture manager requires a queue from the same device.");
            return false;
        }
        device_ = device;
        queue_ = queue;
        return true;
    }

    std::shared_ptr<const Texture2D> TextureManager::Load(const std::filesystem::path& path)
    {
        if (!device_) return {};
        std::error_code error;
        const auto key = path.empty() ? std::filesystem::path{} : std::filesystem::weakly_canonical(path, error);
        if (error)
        {
            Log::Error("Cannot resolve the texture cache path.");
            return {};
        }
        std::pair<std::filesystem::file_time_type,std::filesystem::file_time_type> stamp{};
        if (!key.empty()) {
            stamp.first=std::filesystem::last_write_time(key,error); if(error) return {};
            auto metadata=key; metadata+=".meta";
            if(std::filesystem::exists(metadata,error)) { stamp.second=std::filesystem::last_write_time(metadata,error); if(error) return {}; }
        }
        if (const auto found = textures_.find(key); found != textures_.end() && textureStamps_.at(key)==stamp)
        {
            return found->second;
        }
        auto texture = std::make_shared<Texture2D>();
        if (!texture->Initialize(device_.Get(), queue_.Get(), key)) return {};
        textures_[key]=texture; textureStamps_[key]=stamp;
        return texture;
    }

    bool TextureManager::PathLess::operator()(const std::filesystem::path& left, const std::filesystem::path& right) const
    {
        return CompareStringOrdinal(left.c_str(), -1, right.c_str(), -1, TRUE) == CSTR_LESS_THAN;
    }
}
