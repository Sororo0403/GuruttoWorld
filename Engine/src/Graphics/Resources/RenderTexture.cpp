#include <Engine/Graphics/Resources/RenderTexture.h>
#include <Engine/Graphics/DirectX12/DirectX12Renderer.h>
#include <Engine/Core/Log.h>
#include <Engine/Graphics/Resources/RenderTargetBinding.h>
#include <algorithm>
#include <cmath>
#include <format>

namespace
{
    bool Check(HRESULT result, const char* operation)
    {
        if (SUCCEEDED(result)) return true;
        Engine::Log::Error(std::format("{} failed: 0x{:08X}", operation, static_cast<unsigned long>(result)));
        return false;
    }
}

namespace Engine
{
    struct RenderTexture::Resources
    {
        UINT width = 0, height = 0;
        Microsoft::WRL::ComPtr<ID3D12Device> device;
        Microsoft::WRL::ComPtr<ID3D12Resource> color;
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtv, srv;
        DepthBuffer depth;
    };
    RenderTexture::RenderTexture() = default;
    RenderTexture::~RenderTexture() = default;

    bool RenderTexture::CreateColor(ID3D12Device* device, Resources& resources)
    {
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        heap.CreationNodeMask = heap.VisibleNodeMask = 1;
        D3D12_RESOURCE_DESC description{};
        description.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        description.Width = resources.width;
        description.Height = resources.height;
        description.DepthOrArraySize = description.MipLevels = 1;
        description.Format = Format;
        description.SampleDesc.Count = 1;
        description.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        return Check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &description,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(&resources.color)), "Create render texture");
    }

    bool RenderTexture::CreateViews(ID3D12Device* device, Resources& resources)
    {
        D3D12_DESCRIPTOR_HEAP_DESC heap{};
        heap.NumDescriptors = 1;
        heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        if (!Check(device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&resources.rtv)), "Create texture RTV heap")) return false;
        device->CreateRenderTargetView(resources.color.Get(), nullptr, resources.rtv->GetCPUDescriptorHandleForHeapStart());
        heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        if (!Check(device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&resources.srv)), "Create texture SRV heap")) return false;
        D3D12_SHADER_RESOURCE_VIEW_DESC view{};
        view.Format = Format;
        view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        view.Texture2D.MipLevels = 1;
        device->CreateShaderResourceView(resources.color.Get(), &view, resources.srv->GetCPUDescriptorHandleForHeapStart());
        return resources.depth.Initialize(device, resources.width, resources.height);
    }

    bool RenderTexture::Resize(DirectX12Renderer& renderer, UINT width, UINT height)
    {
        auto* device = renderer.GetDevice();
        if (recording_ || !device || !width || !height ||
            width > D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION || height > D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION) return false;
        if (resources_ && resources_->device.Get() != device) return false;
        if (resources_ && resources_->width == width && resources_->height == height) return true;
        auto candidate = std::make_unique<Resources>();
        candidate->width = width;
        candidate->height = height;
        candidate->device = device;
        if (!CreateColor(device, *candidate) || !CreateViews(device, *candidate) || !renderer.WaitForIdle()) return false;
        resources_.swap(candidate);
        return true;
    }

    void RenderTexture::Transition(ID3D12GraphicsCommandList* commands,
        D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
    {
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = resources_->color.Get();
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = before;
        barrier.Transition.StateAfter = after;
        commands->ResourceBarrier(1, &barrier);
    }

    bool RenderTexture::Begin(ID3D12GraphicsCommandList* commands, const std::array<float, 4>& clearColor)
    {
        if (!resources_ || !commands || recording_ ||
            !std::all_of(clearColor.begin(), clearColor.end(), [](float value) { return std::isfinite(value); })) return false;
        Transition(commands, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
        const auto rtv = resources_->rtv->GetCPUDescriptorHandleForHeapStart();
        const auto dsv = resources_->depth.GetHandle();
        commands->ClearRenderTargetView(rtv, clearColor.data(), 0, nullptr);
        commands->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1, 0, 0, nullptr);
        const D3D12_VIEWPORT viewport{0, 0, static_cast<float>(resources_->width), static_cast<float>(resources_->height), 0, 1};
        const D3D12_RECT scissor{0, 0, static_cast<LONG>(resources_->width), static_cast<LONG>(resources_->height)};
        RenderTargetBinding{rtv,dsv,viewport,scissor}.Bind(commands);
        recording_ = commands;
        return true;
    }

    bool RenderTexture::End(ID3D12GraphicsCommandList* commands)
    {
        if (!commands || recording_ != commands) return false;
        Transition(commands, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        // Unbind the target before sampling; the caller sets the next output target.
        commands->OMSetRenderTargets(0, nullptr, FALSE, nullptr);
        commands->SetPrivateData(RenderTargetBinding::Key,0,nullptr);
        recording_ = nullptr;
        return true;
    }
    UINT RenderTexture::GetWidth() const noexcept { return resources_ ? resources_->width : 0; }
    UINT RenderTexture::GetHeight() const noexcept { return resources_ ? resources_->height : 0; }
    ID3D12Resource* RenderTexture::GetResource() const noexcept { return resources_ ? resources_->color.Get() : nullptr; }
    D3D12_CPU_DESCRIPTOR_HANDLE RenderTexture::GetShaderResourceView() const noexcept
    {
        return resources_ ? resources_->srv->GetCPUDescriptorHandleForHeapStart() : D3D12_CPU_DESCRIPTOR_HANDLE{};
    }
}
