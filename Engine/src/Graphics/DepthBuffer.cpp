#include <Engine/Graphics/DepthBuffer.h>
#include <Engine/Core/Log.h>

#include <format>

namespace
{
    bool Check(HRESULT result, const char* operation)
    {
        if (FAILED(result))
        {
            Engine::Log::Error(std::format("{} failed: 0x{:08X}", operation, static_cast<unsigned long>(result)));
            return false;
        }
        return true;
    }
}

namespace Engine
{
    bool DepthBuffer::Initialize(ID3D12Device* device, UINT width, UINT height)
    {
        if (resource_ || device == nullptr || width == 0 || height == 0)
        {
            return false;
        }
        if (!CreateResource(device, width, height) || !CreateView(device))
        {
            Release();
            return false;
        }
        return true;
    }

    bool DepthBuffer::CreateResource(ID3D12Device* device, UINT width, UINT height)
    {
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        heap.CreationNodeMask = 1;
        heap.VisibleNodeMask = 1;
        D3D12_RESOURCE_DESC description{};
        description.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        description.Width = width;
        description.Height = height;
        description.DepthOrArraySize = 1;
        description.MipLevels = 1;
        description.Format = format;
        description.SampleDesc.Count = 1;
        description.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL | D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
        D3D12_CLEAR_VALUE clear{};
        clear.Format = format;
        clear.DepthStencil.Depth = 1.0f;
        return Check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &description,
            D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear, IID_PPV_ARGS(&resource_)), "Create depth buffer");
    }

    bool DepthBuffer::CreateView(ID3D12Device* device)
    {
        D3D12_DESCRIPTOR_HEAP_DESC description{};
        description.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        description.NumDescriptors = 1;
        if (!Check(device->CreateDescriptorHeap(&description, IID_PPV_ARGS(&heap_)), "Create DSV heap"))
        {
            return false;
        }
        D3D12_DEPTH_STENCIL_VIEW_DESC view{};
        view.Format = format;
        view.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        device->CreateDepthStencilView(resource_.Get(), &view, heap_->GetCPUDescriptorHandleForHeapStart());
        return true;
    }

    void DepthBuffer::Release()
    {
        heap_.Reset();
        resource_.Reset();
    }

    D3D12_CPU_DESCRIPTOR_HANDLE DepthBuffer::GetHandle() const noexcept
    {
        return heap_ ? heap_->GetCPUDescriptorHandleForHeapStart() : D3D12_CPU_DESCRIPTOR_HANDLE{};
    }
}
