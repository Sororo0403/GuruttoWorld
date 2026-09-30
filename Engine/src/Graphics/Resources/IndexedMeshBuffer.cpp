#include <Engine/Graphics/Resources/IndexedMeshBuffer.h>
#include <Engine/Core/Log.h>
#include <Engine/Graphics/DirectX12/GpuSynchronization.h>

#include <algorithm>
#include <cstring>
#include <format>
#include <memory>
#include <utility>

namespace
{
    using Microsoft::WRL::ComPtr;

    bool Check(HRESULT result, const char* operation)
    {
        if (FAILED(result))
        {
            Engine::Log::Error(std::format("{} failed: 0x{:08X}", operation, static_cast<unsigned long>(result)));
            return false;
        }
        return true;
    }

    bool UploadMesh(ID3D12Device* device, ID3D12CommandQueue* queue, ID3D12Resource* destination, ID3D12Resource* upload)
    {
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12GraphicsCommandList> commands;
        ComPtr<ID3D12Fence> fence;
        std::unique_ptr<void, decltype(&CloseHandle)> event(CreateEventW(nullptr, FALSE, FALSE, nullptr), CloseHandle);
        if (!event)
        {
            Engine::Log::Error("Create indexed mesh upload event failed.");
            return false;
        }
        if (!Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)),
                "Create indexed mesh upload allocator") ||
            !Check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr,
                IID_PPV_ARGS(&commands)), "Create indexed mesh upload commands") ||
            !Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)), "Create indexed mesh upload fence"))
        {
            return false;
        }
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = destination;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
        commands->ResourceBarrier(1, &barrier);
        commands->CopyBufferRegion(destination, 0, upload, 0, destination->GetDesc().Width);
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER | D3D12_RESOURCE_STATE_INDEX_BUFFER;
        commands->ResourceBarrier(1, &barrier);
        if (!Check(commands->Close(), "Close indexed mesh upload commands"))
        {
            return false;
        }
        ID3D12CommandList* lists[] = { commands.Get() };
        queue->ExecuteCommandLists(1, lists);
        return Engine::SignalGpuFence(device, queue, fence.Get(), 1) &&
            Engine::WaitForGpuFence(device, fence.Get(), 1, event.get());
    }

    bool ValidateMeshInput(const ID3D12Device* device, ID3D12CommandQueue* queue,
        std::span<const std::byte> vertices, UINT vertexStride, std::span<const std::uint32_t> indices)
    {
        if (device == nullptr || queue == nullptr ||
            queue->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT ||
            vertexStride == 0 || vertexStride % 4 != 0 || vertices.empty() || indices.empty() ||
            vertices.size() % vertexStride != 0 || vertices.size() > UINT_MAX ||
            indices.size() > (UINT_MAX - vertices.size()) / sizeof(std::uint32_t))
        {
            return false;
        }
        const size_t vertexCount = vertices.size() / vertexStride;
        if (std::any_of(indices.begin(), indices.end(), [vertexCount](std::uint32_t index) { return index >= vertexCount; }))
        {
            return false;
        }
        return true;
    }

}

namespace Engine
{
    bool CreateIndexedMeshBuffer(ID3D12Device* device, ID3D12CommandQueue* queue,
        std::span<const std::byte> vertices, UINT vertexStride, std::span<const std::uint32_t> indices,
        Microsoft::WRL::ComPtr<ID3D12Resource>& resource,
        D3D12_VERTEX_BUFFER_VIEW& vertexView, D3D12_INDEX_BUFFER_VIEW& indexView)
    {
        if (resource || !ValidateMeshInput(device, queue, vertices, vertexStride, indices)) return false;
        ComPtr<ID3D12Resource> buffer;
        const UINT vertexBytes = static_cast<UINT>(vertices.size());
        const UINT indexBytes = static_cast<UINT>(indices.size() * sizeof(std::uint32_t));
        const UINT totalBytes = vertexBytes + indexBytes;
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        heap.CreationNodeMask = heap.VisibleNodeMask = 1;
        D3D12_RESOURCE_DESC description{};
        description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        description.Width = totalBytes;
        description.Height = 1;
        description.DepthOrArraySize = 1;
        description.MipLevels = 1;
        description.SampleDesc.Count = 1;
        description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (!Check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &description,
            D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&buffer)), "Create indexed mesh buffer"))
        {
            return false;
        }
        heap.Type = D3D12_HEAP_TYPE_UPLOAD;
        ComPtr<ID3D12Resource> upload;
        if (!Check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &description,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upload)), "Create indexed mesh upload buffer"))
        {
            return false;
        }
        void* mapped = nullptr;
        const D3D12_RANGE readRange{ 0, 0 };
        if (!Check(upload->Map(0, &readRange, &mapped), "Map indexed mesh"))
        {
            return false;
        }
        std::memcpy(mapped, vertices.data(), vertexBytes);
        std::memcpy(static_cast<unsigned char*>(mapped) + vertexBytes, indices.data(), indexBytes);
        const D3D12_RANGE writtenRange{ 0, totalBytes };
        upload->Unmap(0, &writtenRange);
        if (!UploadMesh(device, queue, buffer.Get(), upload.Get()))
        {
            return false;
        }
        resource = std::move(buffer);
        vertexView = { resource->GetGPUVirtualAddress(), vertexBytes, vertexStride };
        indexView = { resource->GetGPUVirtualAddress() + vertexBytes, indexBytes, DXGI_FORMAT_R32_UINT };
        return true;
    }
}
