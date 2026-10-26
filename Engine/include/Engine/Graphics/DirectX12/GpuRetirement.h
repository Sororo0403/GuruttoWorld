#pragma once

#include <Engine/Graphics/DirectX12/GpuSynchronization.h>
#include <wrl/client.h>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <utility>

namespace Engine
{
    /// <summary>送信済み GPU コマンドが使う旧資源を完了フェンスまで保持します。</summary>
    class GpuRetirement final
    {
    public:
        /// <summary>同じデバイスと DIRECT キューの退役管理を共有します。生成失敗時は nullptr です。</summary>
        static std::shared_ptr<GpuRetirement> Shared(ID3D12Device* device, ID3D12CommandQueue* queue);

        /// <summary>資源自身が保持できる独立した退役管理を生成し、共有管理との循環参照を避けます。</summary>
        static std::shared_ptr<GpuRetirement> Dedicated(ID3D12Device* device, ID3D12CommandQueue* queue);

        /// <summary>旧資源を保持して完了値を登録します。失敗時は呼出側の置換・削除を中止してください。</summary>
        bool Retire(std::shared_ptr<void> resource);

        /// <summary>GPU が使用を終えた資源だけを解放します。CPU の待機は行いません。</summary>
        void Collect()
        {
            std::lock_guard lock(mutex_);
            CollectCompleted();
        }

        /// <summary>最後の所有者を破棄するときは保留資源の GPU 使用終了を待ちます。</summary>
        ~GpuRetirement()
        {
            if (!pending_.empty())
                WaitForGpuFence(device_.Get(), fence_.Get(), pending_.back().value, nullptr);
        }

    private:
        struct Pending
        {
            UINT64 value;
            std::shared_ptr<void> resource;
        };
        /// <summary>資源が属するデバイスとキューの寿命を保持します。</summary>
        GpuRetirement(ID3D12Device* device, ID3D12CommandQueue* queue) : device_(device), queue_(queue) {}
        /// <summary>完了済みの保持リスト先頭を取り除きます。</summary>
        void CollectCompleted();
        Microsoft::WRL::ComPtr<ID3D12Device> device_;
        Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue_;
        Microsoft::WRL::ComPtr<ID3D12Fence> fence_;
        UINT64 nextValue_ = 0;
        std::deque<Pending> pending_;
        std::mutex mutex_;
    };
}
