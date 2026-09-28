#include <Engine/Graphics/DirectX12Renderer.h>
#include <Engine/Core/Log.h>

#include <format>
#include <utility>

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
        if (queue && fence && fenceEvent != nullptr)
        {
            WaitForGpu();
        }
#if defined(_DEBUG)
        debugUi_.Shutdown();
#endif
        if (fenceEvent != nullptr)
        {
            CloseHandle(fenceEvent);
            fenceEvent = nullptr;
        }
        commands.Reset();
        for (auto& allocator : allocators)
        {
            allocator.Reset();
        }
        frameFenceValues.fill(0);
        for (auto& buffer : buffers)
        {
            buffer.Reset();
        }
        for (auto& depth : depthBuffers)
        {
            depth.Release();
        }
        renderTargetHeap.Reset();
        swapChain.Reset();
        fence.Reset();
        queue.Reset();
        device.Reset();
        factory.Reset();
        window = nullptr;
        width = 0;
        height = 0;
        descriptorSize = 0;
        fenceValue = 0;
        ready = false;
        occluded = false;
    }

    bool DirectX12Renderer::WaitForGpu()
    {
        const UINT64 target = ++fenceValue;
        if (!Check(queue->Signal(fence.Get(), target), "Signal"))
        {
            return false;
        }
        return WaitForFence(target);
    }

    bool DirectX12Renderer::WaitForFence(UINT64 target)
    {
        if (fence->GetCompletedValue() < target)
        {
            if (!Check(fence->SetEventOnCompletion(target, fenceEvent), "SetEventOnCompletion"))
            {
                return false;
            }
            if (WaitForSingleObject(fenceEvent, INFINITE) != WAIT_OBJECT_0)
            {
                Log::Error("GPU fence wait failed.");
                return false;
            }
        }
        return Check(device->GetDeviceRemovedReason(), "GPU device status");
    }

    bool DirectX12Renderer::CreateRenderTargets()
    {
        auto descriptor = renderTargetHeap->GetCPUDescriptorHandleForHeapStart();
        for (UINT index = 0; index < bufferCount; ++index)
        {
            if (!Check(swapChain->GetBuffer(index, IID_PPV_ARGS(&buffers[index])), "GetBuffer"))
            {
                return false;
            }
            device->CreateRenderTargetView(buffers[index].Get(), nullptr, descriptor);
            descriptor.ptr += descriptorSize;
        }
        return true;
    }

    bool DirectX12Renderer::CreateDepthBuffers()
    {
        for (auto& depth : depthBuffers)
        {
            if (!depth.Initialize(device.Get(), width, height))
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
        window = handle;
        width = static_cast<UINT>(client.right);
        height = static_cast<UINT>(client.bottom);

        return true;
    }

    void DirectX12Renderer::EnableDebugLayer()
    {
#if defined(_DEBUG)
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
        return Check(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)), "CreateDXGIFactory2");
    }

    bool DirectX12Renderer::CreateDevice()
    {
        for (UINT index = 0; ; ++index)
        {
            ComPtr<IDXGIAdapter1> adapter;
            const HRESULT result = factory->EnumAdapters1(index, &adapter);
            if (result == DXGI_ERROR_NOT_FOUND)
            {
                break;
            }
            if (!Check(result, "EnumAdapters1"))
            {
                return false;
            }
            DXGI_ADAPTER_DESC1 description{};
            if (!Check(adapter->GetDesc1(&description), "GetDesc1"))
            {
                return false;
            }
            if ((description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0 &&
                SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0,
                    IID_PPV_ARGS(&device))))
            {
                break;
            }
        }
        if (!device)
        {
            ComPtr<IDXGIAdapter> warp;
            if (!Check(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)), "EnumWarpAdapter") ||
                !Check(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0,
                    IID_PPV_ARGS(&device)), "D3D12CreateDevice"))
            {
                return false;
            }
            Log::Warning("DirectX 12 is using the WARP software adapter.");
        }

        return true;
    }

    bool DirectX12Renderer::CreateCommandQueue()
    {
        D3D12_COMMAND_QUEUE_DESC queueDescription{};
        queueDescription.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        if (!Check(device->CreateCommandQueue(&queueDescription, IID_PPV_ARGS(&queue)), "CreateCommandQueue"))
        {
            return false;
        }
        return true;
    }

    bool DirectX12Renderer::CreateSwapChain()
    {
        DXGI_SWAP_CHAIN_DESC1 swapDescription{};
        swapDescription.Width = width;
        swapDescription.Height = height;
        swapDescription.Format = bufferFormat;
        swapDescription.SampleDesc.Count = 1;
        swapDescription.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapDescription.BufferCount = bufferCount;
        swapDescription.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        ComPtr<IDXGISwapChain1> initialSwapChain;
        if (!Check(factory->CreateSwapChainForHwnd(queue.Get(), window, &swapDescription,
            nullptr, nullptr, &initialSwapChain), "CreateSwapChainForHwnd") ||
            !Check(initialSwapChain.As(&swapChain), "Query IDXGISwapChain3") ||
            !Check(factory->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER), "MakeWindowAssociation"))
        {
            return false;
        }

        return true;
    }

    bool DirectX12Renderer::CreateRenderTargetHeap()
    {
        D3D12_DESCRIPTOR_HEAP_DESC heapDescription{};
        heapDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        heapDescription.NumDescriptors = bufferCount;
        if (!Check(device->CreateDescriptorHeap(&heapDescription, IID_PPV_ARGS(&renderTargetHeap)), "CreateDescriptorHeap"))
        {
            return false;
        }
        descriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        return true;
    }

    bool DirectX12Renderer::CreateDrawingCommands()
    {
        for (auto& allocator : allocators)
        {
            if (!Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                IID_PPV_ARGS(&allocator)), "CreateCommandAllocator"))
            {
                return false;
            }
        }
        if (!Check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
            allocators[0].Get(), nullptr, IID_PPV_ARGS(&commands)), "CreateCommandList"))
        {
            return false;
        }
        return Check(commands->Close(), "Close initial command list");
    }

    bool DirectX12Renderer::CreateSynchronizationObjects()
    {
        if (!Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)), "CreateFence"))
        {
            return false;
        }
        fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (fenceEvent == nullptr)
        {
            Log::Error("Cannot create the GPU fence event.");
            return false;
        }
        return true;
    }

    bool DirectX12Renderer::Initialize(HWND handle)
    {
        if (window != nullptr)
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
#if defined(_DEBUG)
        if (!debugUi_.Initialize(window, device.Get(), queue.Get(), static_cast<int>(bufferCount), bufferFormat))
        {
            ReleaseResources();
            return false;
        }
#endif
        ready = true;
        Log::Info("DirectX 12 renderer initialized.");
        return true;
    }

    ID3D12Device* DirectX12Renderer::GetDevice() const noexcept
    {
        return device.Get();
    }

    ID3D12CommandQueue* DirectX12Renderer::GetCommandQueue() const noexcept
    {
        return queue.Get();
    }

    UINT DirectX12Renderer::GetWidth() const noexcept
    {
        return width;
    }

    UINT DirectX12Renderer::GetHeight() const noexcept
    {
        return height;
    }

    RenderResult DirectX12Renderer::Render(const std::array<float, 4>& clearColor,
        const std::function<void(ID3D12GraphicsCommandList*, float)>& draw,
        const std::function<void()>& debugUi)
    {
#if !defined(_DEBUG)
        (void)debugUi;
#endif
        if (!ready)
        {
            return RenderResult::Failed;
        }
        RECT client{};
        if (!GetClientRect(window, &client))
        {
            Log::Error("Cannot get the render target size.");
            return RenderResult::Failed;
        }
        if (IsIconic(window) || client.right <= 0 || client.bottom <= 0)
        {
            return RenderResult::Paused;
        }
        if (occluded)
        {
            const HRESULT visibility = swapChain->Present(0, DXGI_PRESENT_TEST);
            if (visibility == DXGI_STATUS_OCCLUDED)
            {
                return RenderResult::Paused;
            }
            if (!Check(visibility, "Test swap chain visibility"))
            {
                return RenderResult::Failed;
            }
            occluded = false;
        }
        const UINT newWidth = static_cast<UINT>(client.right);
        const UINT newHeight = static_cast<UINT>(client.bottom);
        if (newWidth != width || newHeight != height)
        {
            if (!WaitForGpu())
            {
                return RenderResult::Failed;
            }
            for (auto& buffer : buffers)
            {
                buffer.Reset();
            }
            if (!Check(swapChain->ResizeBuffers(bufferCount, newWidth, newHeight,
                bufferFormat, 0), "ResizeBuffers") || !CreateRenderTargets())
            {
                ready = false;
                return RenderResult::Failed;
            }
            for (auto& depth : depthBuffers)
            {
                depth.Release();
            }
            width = newWidth;
            height = newHeight;
            if (!CreateDepthBuffers())
            {
                ready = false;
                return RenderResult::Failed;
            }
            Log::Info(std::format("DirectX 12 resized: {}x{}", width, height));
        }

        const UINT index = swapChain->GetCurrentBackBufferIndex();
        if (!WaitForFence(frameFenceValues[index]) ||
            !Check(allocators[index]->Reset(), "Reset command allocator") ||
            !Check(commands->Reset(allocators[index].Get(), nullptr), "Reset command list"))
        {
            return RenderResult::Failed;
        }
#if defined(_DEBUG)
        debugUi_.BeginFrame();
        if (debugUi)
        {
            debugUi();
        }
#endif
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = buffers[index].Get();
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        commands->ResourceBarrier(1, &barrier);
        auto descriptor = renderTargetHeap->GetCPUDescriptorHandleForHeapStart();
        descriptor.ptr += static_cast<SIZE_T>(index) * descriptorSize;
        commands->ClearRenderTargetView(descriptor, clearColor.data(), 0, nullptr);
        const auto depthDescriptor = depthBuffers[index].GetHandle();
        commands->ClearDepthStencilView(depthDescriptor, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
        commands->OMSetRenderTargets(1, &descriptor, FALSE, &depthDescriptor);
        const D3D12_VIEWPORT viewport{ 0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f };
        const D3D12_RECT scissor{ 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) };
        commands->RSSetViewports(1, &viewport);
        commands->RSSetScissorRects(1, &scissor);
        if (draw)
        {
            draw(commands.Get(), static_cast<float>(width) / static_cast<float>(height));
        }
#if defined(_DEBUG)
        // UI はシーンの深度に影響されないよう、深度バッファーを外して描画します。
        commands->OMSetRenderTargets(1, &descriptor, FALSE, nullptr);
        debugUi_.Render(commands.Get());
#endif
        std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
        commands->ResourceBarrier(1, &barrier);
        if (!Check(commands->Close(), "Close command list"))
        {
            return RenderResult::Failed;
        }
        ID3D12CommandList* lists[] = { commands.Get() };
        queue->ExecuteCommandLists(1, lists);
        const HRESULT present = swapChain->Present(1, 0);
        // このフレームの完了値を記録し、同じバッファーを再利用するときだけ待機します。
        const UINT64 submittedFence = ++fenceValue;
        if (!Check(queue->Signal(fence.Get(), submittedFence), "Signal frame fence"))
        {
            return RenderResult::Failed;
        }
        frameFenceValues[index] = submittedFence;
        if (!Check(present, "Present"))
        {
            return RenderResult::Failed;
        }
        if (present == DXGI_STATUS_OCCLUDED)
        {
            occluded = true;
            return RenderResult::Paused;
        }
        return RenderResult::Presented;
    }
}
