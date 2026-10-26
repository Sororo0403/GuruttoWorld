#include <Engine/Graphics/DirectX12/GpuRetirement.h>

namespace Engine
{
    auto GpuRetirement::Shared(ID3D12Device* device, ID3D12CommandQueue* queue) -> std::shared_ptr<GpuRetirement>
    {
        if (!device || !queue || queue->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT)
            return {};
        Microsoft::WRL::ComPtr<ID3D12Device> queueDevice;
        if (FAILED(queue->GetDevice(IID_PPV_ARGS(&queueDevice))) || queueDevice.Get() != device)
            return {};
        static std::mutex mutex;
        static std::map<std::pair<ID3D12Device*, ID3D12CommandQueue*>, std::weak_ptr<GpuRetirement>> contexts;
        std::lock_guard lock(mutex);
        std::erase_if(contexts,
                      [](const auto& item)
                      {
                          return item.second.expired();
                      });
        const auto key = std::make_pair(device, queue);
        if (const auto found = contexts.find(key); found != contexts.end())
            if (auto shared = found->second.lock())
                return shared;
        auto shared = std::shared_ptr<GpuRetirement>(new GpuRetirement(device, queue));
        if (FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&shared->fence_))))
            return {};
        contexts.insert_or_assign(key, shared);
        return shared;
    }

    auto GpuRetirement::Dedicated(ID3D12Device* device, ID3D12CommandQueue* queue) -> std::shared_ptr<GpuRetirement>
    {
        if (!device || !queue || queue->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT)
            return {};
        Microsoft::WRL::ComPtr<ID3D12Device> queueDevice;
        if (FAILED(queue->GetDevice(IID_PPV_ARGS(&queueDevice))) || queueDevice.Get() != device)
            return {};
        auto context = std::shared_ptr<GpuRetirement>(new GpuRetirement(device, queue));
        if (FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&context->fence_))))
            return {};
        return context;
    }

    auto GpuRetirement::Retire(std::shared_ptr<void> resource) -> bool
    {
        if (!resource)
            return true;
        std::lock_guard lock(mutex_);
        const auto value = nextValue_ + 1;
        pending_.push_back({value, std::move(resource)});
        if (!SignalGpuFence(device_.Get(), queue_.Get(), fence_.Get(), value))
        {
            pending_.pop_back();
            return false;
        }
        nextValue_ = value;
        CollectCompleted();
        return true;
    }

    auto GpuRetirement::CollectCompleted() -> void
    {
        const auto completed = fence_->GetCompletedValue();
        while (!pending_.empty() && pending_.front().value <= completed)
            pending_.pop_front();
    }
}
