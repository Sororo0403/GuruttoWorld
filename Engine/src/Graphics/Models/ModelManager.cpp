#include <Engine/Graphics/Models/ModelManager.h>
#include <Engine/Core/Log.h>

namespace Engine
{
    bool ModelManager::Initialize(ID3D12Device* device, ID3D12CommandQueue* queue, const std::filesystem::path& shaderPath)
    {
        if (shaderPath.empty() || device_ || device == nullptr || queue == nullptr || queue->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT)
        {
            return false;
        }
        Microsoft::WRL::ComPtr<ID3D12Device> queueDevice;
        if (FAILED(queue->GetDevice(IID_PPV_ARGS(&queueDevice))) || queueDevice.Get() != device)
        {
            Log::Error("Model manager requires a queue from the same device.");
            return false;
        }
        shaderPath_ = shaderPath;
        device_ = device;
        queue_ = queue;
        return true;
    }

    std::shared_ptr<const ModelRenderer> ModelManager::Load(const std::filesystem::path& path)
    {
        if (!device_ || path.empty()) return {};
        std::error_code error;
        const auto key = std::filesystem::weakly_canonical(path, error);
        if (error)
        {
            const auto utf8=path.generic_u8string();
            Log::Error("Cannot resolve the model cache path: "+std::string(utf8.begin(),utf8.end())+" ("+error.message()+")");
            return {};
        }
        if (const auto found = models_.find(key); found != models_.end())
        {
            return found->second;
        }
        auto model = std::make_shared<ModelRenderer>();
        if (!model->Initialize(device_.Get(), queue_.Get(), key, shaderPath_)) return {};
        models_.emplace(key, model);
        return model;
    }

    void ModelManager::Swap(ModelManager& other) noexcept
    {
        device_.Swap(other.device_); queue_.Swap(other.queue_);
        shaderPath_.swap(other.shaderPath_); models_.swap(other.models_);
    }

    bool ModelManager::PathLess::operator()(const std::filesystem::path& left, const std::filesystem::path& right) const
    {
        return CompareStringOrdinal(left.c_str(), -1, right.c_str(), -1, TRUE) == CSTR_LESS_THAN;
    }
}
