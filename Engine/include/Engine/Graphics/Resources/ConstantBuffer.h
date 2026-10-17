#pragma once
#include <Engine/Graphics/Resources/RenderFrameContext.h>
#include <wrl/client.h>
#include <array>
#include <vector>
#include <span>
#include <limits>
#include <cstring>

namespace Engine
{
    // Each draw gets immutable data until its frame slot's fence has completed.
    class ConstantBuffer final
    {
    public:
        D3D12_GPU_VIRTUAL_ADDRESS Prepare(ID3D12Device* device, ID3D12GraphicsCommandList* commands,
                                          std::span<const float> values)
        {
            RenderFrameContext context;
            if (!device || values.empty() || values.size_bytes() > 1024 ||
                !RenderFrameContext::Current(commands, context))
                return 0;
            auto& frame = frames_[context.slot];
            if (frame.serial != context.serial)
            {
                frame.serial = context.serial;
                frame.used = 0;
            }
            if (frame.used >= 4096)
                return 0;
            if (frame.used == frame.buffers.size())
            {
                Microsoft::WRL::ComPtr<ID3D12Resource> buffer;
                D3D12_HEAP_PROPERTIES heap{};
                heap.Type = D3D12_HEAP_TYPE_UPLOAD;
                D3D12_RESOURCE_DESC description{};
                description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
                description.Width = 1024;
                description.Height = 1;
                description.DepthOrArraySize = description.MipLevels = 1;
                description.SampleDesc.Count = 1;
                description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
                if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &description,
                                                           D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                           IID_PPV_ARGS(&buffer))))
                    return 0;
                frame.buffers.push_back(std::move(buffer));
            }
            auto& buffer = frame.buffers[frame.used++];
            void* destination = nullptr;
            const D3D12_RANGE read{0, 0};
            if (FAILED(buffer->Map(0, &read, &destination)))
                return 0;
            std::memcpy(destination, values.data(), values.size_bytes());
            const D3D12_RANGE written{0, values.size_bytes()};
            buffer->Unmap(0, &written);
            return buffer->GetGPUVirtualAddress();
        }

    private:
        struct Frame
        {
            UINT64 serial = std::numeric_limits<UINT64>::max();
            size_t used = 0;
            std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> buffers;
        };
        std::array<Frame, RenderFrameContext::SlotCount> frames_;
    };
}
