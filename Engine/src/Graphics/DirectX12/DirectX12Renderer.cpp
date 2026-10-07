#include <Engine/Graphics/DirectX12/DirectX12Renderer.h>
#include <Engine/Core/Log.h>
#include <Engine/Graphics/DirectX12/GpuSynchronization.h>

#include <format>

#pragma comment(lib, "D3D12.lib")
#pragma comment(lib, "DXGI.lib")

namespace
{
    using Microsoft::WRL::ComPtr;

    bool Check(HRESULT result, const char* operation)
    {
        if (FAILED(result))
        {
            Engine::Log::Error(std::format("{} failed: 0x{:08X}", operation,
                static_cast<unsigned long>(result)));
            return false;
        }
        return true;
    }

    void LogSelectedAdapter(const DXGI_ADAPTER_DESC1& description)
    {
        std::array<char, 512> name{};
        const int length = WideCharToMultiByte(CP_UTF8, 0, description.Description, -1,
            name.data(), static_cast<int>(name.size()), nullptr, nullptr);
        Engine::Log::Info(std::format(
            "DirectX 12 GPU: {} (dedicated VRAM: {} MiB, vendor: {:04X}, device: {:04X}, LUID: {:08X}:{:08X}).",
            length > 0 ? name.data() : "Unknown GPU",
            description.DedicatedVideoMemory / (1024 * 1024), description.VendorId, description.DeviceId,
            static_cast<unsigned int>(description.AdapterLuid.HighPart), description.AdapterLuid.LowPart));
    }
}

namespace Engine
{
    DirectX12Renderer::DirectX12Renderer() = default;

    DirectX12Renderer::~DirectX12Renderer()
    {
        ReleaseResources();
    }

    void DirectX12Renderer::ReleaseResources()
    {
        if (queue_ && fence_ && fenceEvent_ != nullptr)
        {
            if (!WaitForGpu())
            {
                Log::Warning("Releasing rendering resources after confirmed device loss.");
            }
        }
#if defined(_DEBUG) || defined(ENGINE_DEVELOPMENT)
        debugUi_.Shutdown();
#endif
        if (fenceEvent_ != nullptr)
        {
            CloseHandle(fenceEvent_);
            fenceEvent_ = nullptr;
        }
        commands_.Reset();
        for (auto& allocator : allocators_)
        {
            allocator.Reset();
        }
        frameFenceValues_.fill(0);
        for (auto& buffer : buffers_)
        {
            buffer.Reset();
        }
        for (auto& depth : depthBuffers_)
        {
            depth.Release();
        }
        renderTargetHeap_.Reset();
        swapChain_.Reset();
        fence_.Reset();
        queue_.Reset();
        device_.Reset();
        factory_.Reset();
        window_ = nullptr;
        width_ = 0;
        height_ = 0;
        descriptorSize_ = 0;
        fenceValue_ = 0;
        ready_ = false;
        occluded_ = false;
    }

#if defined(_DEBUG) || defined(ENGINE_DEVELOPMENT)
    D3D12_GPU_DESCRIPTOR_HANDLE DirectX12Renderer::SetSceneTexture(D3D12_CPU_DESCRIPTOR_HANDLE source, unsigned int slot)
    {
        return debugUi_.SetSceneTexture(device_.Get(), source, slot);
    }
#endif

    bool DirectX12Renderer::WaitForIdle()
    {
        return ready_ && WaitForGpu();
    }

    bool DirectX12Renderer::WaitForGpu()
    {
        const UINT64 target = ++fenceValue_;
        return SignalGpuFence(device_.Get(), queue_.Get(), fence_.Get(), target) && WaitForFence(target);
    }

    bool DirectX12Renderer::WaitForFence(UINT64 target)
    {
        return WaitForGpuFence(device_.Get(), fence_.Get(), target, fenceEvent_);
    }

    bool DirectX12Renderer::CreateRenderTargets()
    {
        auto descriptor = renderTargetHeap_->GetCPUDescriptorHandleForHeapStart();
        for (UINT index = 0; index < BufferCount; ++index)
        {
            if (!Check(swapChain_->GetBuffer(index, IID_PPV_ARGS(&buffers_[index])), "GetBuffer"))
            {
                return false;
            }
            device_->CreateRenderTargetView(buffers_[index].Get(), nullptr, descriptor);
            descriptor.ptr += descriptorSize_;
        }
        return true;
    }

    bool DirectX12Renderer::CreateDepthBuffers()
    {
        for (auto& depth : depthBuffers_)
        {
            if (!depth.Initialize(device_.Get(), width_, height_))
            {
                return false;
            }
        }
        return true;
    }

    bool DirectX12Renderer::SetTargetWindow(HWND handle)
    {
        RECT client{};
        if (!IsWindow(handle) || !GetClientRect(handle, &client) ||
            client.right <= 0 || client.bottom <= 0)
        {
            Log::Error("Invalid DirectX 12 target window.");
            return false;
        }
        window_ = handle;
        width_ = static_cast<UINT>(client.right);
        height_ = static_cast<UINT>(client.bottom);

        return true;
    }

    void DirectX12Renderer::EnableDebugLayer()
    {
#if defined(_DEBUG) || defined(ENGINE_DEVELOPMENT)
        ComPtr<ID3D12Debug> debug;
        const HRESULT result = D3D12GetDebugInterface(IID_PPV_ARGS(&debug));
        if (SUCCEEDED(result))
        {
            debug->EnableDebugLayer();
            Log::Info("DirectX 12 debug layer enabled.");
        }
        else
        {
            Log::Warning(std::format(
                "DirectX 12 debug layer unavailable: 0x{:08X}. Check Windows Graphics Tools installation.",
                static_cast<unsigned long>(result)));
        }
#endif
    }

    bool DirectX12Renderer::CreateFactory()
    {
        return Check(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory_)), "CreateDXGIFactory2");
    }

    bool DirectX12Renderer::CreateDevice()
    {
        ComPtr<IDXGIFactory6> preferredFactory;
        const bool useGpuPreference = SUCCEEDED(factory_.As(&preferredFactory));
        DXGI_ADAPTER_DESC1 selectedDescription{};
        if (!useGpuPreference)
        {
            Log::Warning("DXGI GPU preference is unavailable; selecting the compatible GPU with the most dedicated VRAM.");
        }
        for (UINT index = 0; ; ++index)
        {
            ComPtr<IDXGIAdapter1> adapter;
            const HRESULT result = useGpuPreference
                ? preferredFactory->EnumAdapterByGpuPreference(index, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                    IID_PPV_ARGS(&adapter))
                : factory_->EnumAdapters1(index, &adapter);
            if (result == DXGI_ERROR_NOT_FOUND)
            {
                break;
            }
            if (!Check(result, useGpuPreference ? "EnumAdapterByGpuPreference" : "EnumAdapters1"))
            {
                return false;
            }
            DXGI_ADAPTER_DESC1 description{};
            if (!Check(adapter->GetDesc1(&description), "GetDesc1"))
            {
                return false;
            }
            if ((description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0)
            {
                continue;
            }
            ComPtr<ID3D12Device> candidate;
            if (FAILED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0,
                IID_PPV_ARGS(&candidate))))
            {
                continue;
            }
            if (!device_ || description.DedicatedVideoMemory > selectedDescription.DedicatedVideoMemory)
            {
                device_ = candidate;
                selectedDescription = description;
            }
            if (useGpuPreference)
            {
                break;
            }
        }
        if (device_)
        {
            LogSelectedAdapter(selectedDescription);
            return true;
        }
        return CreateWarpDevice();
    }

    bool DirectX12Renderer::CreateWarpDevice()
    {
        ComPtr<IDXGIAdapter> warp;
        if (!Check(factory_->EnumWarpAdapter(IID_PPV_ARGS(&warp)), "EnumWarpAdapter") ||
            !Check(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0,
                IID_PPV_ARGS(&device_)), "D3D12CreateDevice"))
        {
            return false;
        }
        Log::Warning("DirectX 12 is using the WARP software adapter.");

        return true;
    }

    bool DirectX12Renderer::CreateCommandQueue()
    {
        D3D12_COMMAND_QUEUE_DESC queueDescription{};
        queueDescription.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        if (!Check(device_->CreateCommandQueue(&queueDescription, IID_PPV_ARGS(&queue_)), "CreateCommandQueue"))
        {
            return false;
        }
        return true;
    }

    bool DirectX12Renderer::CreateSwapChain()
    {
        DXGI_SWAP_CHAIN_DESC1 swapDescription{};
        swapDescription.Width = width_;
        swapDescription.Height = height_;
        swapDescription.Format = BufferFormat;
        swapDescription.SampleDesc.Count = 1;
        swapDescription.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapDescription.BufferCount = BufferCount;
        swapDescription.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        ComPtr<IDXGISwapChain1> initialSwapChain;
        if (!Check(factory_->CreateSwapChainForHwnd(queue_.Get(), window_, &swapDescription,
            nullptr, nullptr, &initialSwapChain), "CreateSwapChainForHwnd") ||
            !Check(initialSwapChain.As(&swapChain_), "Query IDXGISwapChain3") ||
            !Check(factory_->MakeWindowAssociation(window_, DXGI_MWA_NO_ALT_ENTER), "MakeWindowAssociation"))
        {
            return false;
        }

        return true;
    }

    bool DirectX12Renderer::CreateRenderTargetHeap()
    {
        D3D12_DESCRIPTOR_HEAP_DESC heapDescription{};
        heapDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        heapDescription.NumDescriptors = BufferCount;
        if (!Check(device_->CreateDescriptorHeap(&heapDescription, IID_PPV_ARGS(&renderTargetHeap_)), "CreateDescriptorHeap"))
        {
            return false;
        }
        descriptorSize_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        return true;
    }

    bool DirectX12Renderer::CreateDrawingCommands()
    {
        for (auto& allocator : allocators_)
        {
            if (!Check(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                IID_PPV_ARGS(&allocator)), "CreateCommandAllocator"))
            {
                return false;
            }
        }
        if (!Check(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
            allocators_[0].Get(), nullptr, IID_PPV_ARGS(&commands_)), "CreateCommandList"))
        {
            return false;
        }
        return Check(commands_->Close(), "Close initial command list");
    }

    bool DirectX12Renderer::CreateSynchronizationObjects()
    {
        if (!Check(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)), "CreateFence"))
        {
            return false;
        }
        fenceEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (fenceEvent_ == nullptr)
        {
            Log::Error("Cannot create the GPU fence event.");
            return false;
        }
        return true;
    }

    bool DirectX12Renderer::Initialize(HWND handle)
    {
        if (window_ != nullptr)
        {
            return false;
        }
        if (!SetTargetWindow(handle))
        {
            return false;
        }
        EnableDebugLayer();
        if (!CreateFactory() ||
            !CreateDevice() ||
            !CreateCommandQueue() ||
            !CreateSwapChain() ||
            !CreateRenderTargetHeap() ||
            !CreateRenderTargets() ||
            !CreateDepthBuffers() ||
            !CreateDrawingCommands() ||
            !CreateSynchronizationObjects())
        {
            ReleaseResources();
            return false;
        }
#if defined(_DEBUG) || defined(ENGINE_DEVELOPMENT)
        if (!debugUi_.Initialize(window_, device_.Get(), queue_.Get(), static_cast<int>(BufferCount), BufferFormat))
        {
            ReleaseResources();
            return false;
        }
#endif
        ready_ = true;
        Log::Info("DirectX 12 renderer initialized.");
        return true;
    }

    ID3D12Device* DirectX12Renderer::GetDevice() const noexcept
    {
        return device_.Get();
    }

    ID3D12CommandQueue* DirectX12Renderer::GetCommandQueue() const noexcept
    {
        return queue_.Get();
    }

    UINT DirectX12Renderer::GetWidth() const noexcept
    {
        return width_;
    }

    UINT DirectX12Renderer::GetHeight() const noexcept
    {
        return height_;
    }

    RenderResult DirectX12Renderer::Render(const std::array<float, 4>& clearColor,
        const std::function<void(ID3D12GraphicsCommandList*, float)>& draw,
        const std::function<void()>& debugUi)
    {
        if (!ready_)
        {
            return RenderResult::Failed;
        }
        RECT client{};
        if (!GetClientRect(window_, &client))
        {
            Log::Error("Cannot get the render target size.");
            return RenderResult::Failed;
        }
        if (IsIconic(window_) || client.right <= 0 || client.bottom <= 0)
        {
            return RenderResult::Paused;
        }
        if (occluded_)
        {
            const HRESULT visibility = swapChain_->Present(0, DXGI_PRESENT_TEST);
            if (visibility == DXGI_STATUS_OCCLUDED)
            {
                return RenderResult::Paused;
            }
            if (!Check(visibility, "Test swap chain visibility"))
            {
                return RenderResult::Failed;
            }
            occluded_ = false;
        }
        const UINT newWidth = static_cast<UINT>(client.right);
        const UINT newHeight = static_cast<UINT>(client.bottom);
        if ((newWidth != width_ || newHeight != height_) && !Resize(newWidth, newHeight))
        {
            return RenderResult::Failed;
        }
        const UINT index = swapChain_->GetCurrentBackBufferIndex();
        if (!BeginFrame(index, clearColor, debugUi))
        {
            return RenderResult::Failed;
        }
        if (draw)
        {
            draw(commands_.Get(), static_cast<float>(width_) / static_cast<float>(height_));
        }
        return EndFrame(index);
    }

    bool DirectX12Renderer::Resize(UINT newWidth, UINT newHeight)
    {
        if (!WaitForGpu())
        {
            return false;
        }
        for (auto& buffer : buffers_)
        {
            buffer.Reset();
        }
        if (!Check(swapChain_->ResizeBuffers(BufferCount, newWidth, newHeight,
            BufferFormat, 0), "ResizeBuffers") || !CreateRenderTargets())
        {
            ready_ = false;
            return false;
        }
        for (auto& depth : depthBuffers_)
        {
            depth.Release();
        }
        width_ = newWidth;
        height_ = newHeight;
        if (!CreateDepthBuffers())
        {
            ready_ = false;
            return false;
        }
        Log::Info(std::format("DirectX 12 resized: {}x{}", width_, height_));
        return true;
    }

    bool DirectX12Renderer::BeginFrame(UINT index, const std::array<float, 4>& clearColor,
        const std::function<void()>& debugUi)
    {
#if !defined(_DEBUG) && !defined(ENGINE_DEVELOPMENT)
        (void)debugUi;
#endif
        if (!WaitForFence(frameFenceValues_[index]) ||
            !Check(allocators_[index]->Reset(), "Reset command allocator") ||
            !Check(commands_->Reset(allocators_[index].Get(), nullptr), "Reset command list"))
        {
            return false;
        }
#if defined(_DEBUG) || defined(ENGINE_DEVELOPMENT)
        debugUi_.BeginFrame();
        if (debugUi)
        {
            debugUi();
        }
#endif
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = buffers_[index].Get();
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        commands_->ResourceBarrier(1, &barrier);
        auto descriptor = renderTargetHeap_->GetCPUDescriptorHandleForHeapStart();
        descriptor.ptr += static_cast<SIZE_T>(index) * descriptorSize_;
        commands_->ClearRenderTargetView(descriptor, clearColor.data(), 0, nullptr);
        const auto depthDescriptor = depthBuffers_[index].GetHandle();
        commands_->ClearDepthStencilView(depthDescriptor, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
        commands_->OMSetRenderTargets(1, &descriptor, FALSE, &depthDescriptor);
        const D3D12_VIEWPORT viewport{ 0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_), 0.0f, 1.0f };
        const D3D12_RECT scissor{ 0, 0, static_cast<LONG>(width_), static_cast<LONG>(height_) };
        commands_->RSSetViewports(1, &viewport);
        commands_->RSSetScissorRects(1, &scissor);
        return true;
    }

    RenderResult DirectX12Renderer::EndFrame(UINT index)
    {
#if defined(_DEBUG) || defined(ENGINE_DEVELOPMENT)
        // UI はシーンの深度に影響されないよう、深度バッファーを外して描画します。
        auto descriptor = renderTargetHeap_->GetCPUDescriptorHandleForHeapStart();
        descriptor.ptr += static_cast<SIZE_T>(index) * descriptorSize_;
        commands_->OMSetRenderTargets(1, &descriptor, FALSE, nullptr);
        const D3D12_VIEWPORT viewport{0, 0, static_cast<float>(width_), static_cast<float>(height_), 0, 1};
        const D3D12_RECT scissor{0, 0, static_cast<LONG>(width_), static_cast<LONG>(height_)};
        commands_->RSSetViewports(1, &viewport);
        commands_->RSSetScissorRects(1, &scissor);
        debugUi_.Render(commands_.Get());
#endif
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = buffers_[index].Get();
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        commands_->ResourceBarrier(1, &barrier);
        if (!Check(commands_->Close(), "Close command list"))
        {
            return RenderResult::Failed;
        }
        ID3D12CommandList* lists[] = { commands_.Get() };
        queue_->ExecuteCommandLists(1, lists);
        const HRESULT present = swapChain_->Present(1, 0);
        // このフレームの完了値を記録し、同じバッファーを再利用するときだけ待機します。
        const UINT64 submittedFence = ++fenceValue_;
        if (!SignalGpuFence(device_.Get(), queue_.Get(), fence_.Get(), submittedFence))
        {
            return RenderResult::Failed;
        }
        frameFenceValues_[index] = submittedFence;
        if (!Check(present, "Present"))
        {
            return RenderResult::Failed;
        }
        if (present == DXGI_STATUS_OCCLUDED)
        {
            occluded_ = true;
            return RenderResult::Paused;
        }
        return RenderResult::Presented;
    }
}
