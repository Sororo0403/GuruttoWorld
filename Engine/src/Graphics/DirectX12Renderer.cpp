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
        if (fenceEvent != nullptr)
        {
            CloseHandle(fenceEvent);
            fenceEvent = nullptr;
        }
        commands.Reset();
        allocator.Reset();
        for (auto& buffer : buffers)
        {
            buffer.Reset();
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
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))
        {
            debug->EnableDebugLayer();
            Log::Info("DirectX 12 debug layer enabled.");
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
        if (!Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_PPV_ARGS(&allocator)), "CreateCommandAllocator") ||
            !Check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                allocator.Get(), nullptr, IID_PPV_ARGS(&commands)), "CreateCommandList"))
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
            !CreateDrawingCommands() ||
            !CreateSynchronizationObjects())
        {
            ReleaseResources();
            return false;
        }
        ready = true;
        Log::Info("DirectX 12 renderer initialized.");
        return true;
    }

    bool DirectX12Renderer::Render(const std::array<float, 4>& clearColor)
    {
        if (!ready)
        {
            return false;
        }
        RECT client{};
        if (!GetClientRect(window, &client))
        {
            Log::Error("Cannot get the render target size.");
            return false;
        }
        if (IsIconic(window) || client.right <= 0 || client.bottom <= 0)
        {
            Sleep(16);
            return true;
        }
        if (occluded)
        {
            const HRESULT visibility = swapChain->Present(0, DXGI_PRESENT_TEST);
            if (visibility == DXGI_STATUS_OCCLUDED)
            {
                Sleep(50);
                return true;
            }
            if (!Check(visibility, "Test swap chain visibility"))
            {
                return false;
            }
            occluded = false;
        }
        const UINT newWidth = static_cast<UINT>(client.right);
        const UINT newHeight = static_cast<UINT>(client.bottom);
        if (newWidth != width || newHeight != height)
        {
            if (!WaitForGpu())
            {
                return false;
            }
            for (auto& buffer : buffers)
            {
                buffer.Reset();
            }
            if (!Check(swapChain->ResizeBuffers(bufferCount, newWidth, newHeight,
                bufferFormat, 0), "ResizeBuffers") || !CreateRenderTargets())
            {
                ready = false;
                return false;
            }
            width = newWidth;
            height = newHeight;
            Log::Info(std::format("DirectX 12 resized: {}x{}", width, height));
        }

        if (!Check(allocator->Reset(), "Reset command allocator") ||
            !Check(commands->Reset(allocator.Get(), nullptr), "Reset command list"))
        {
            return false;
        }
        const UINT index = swapChain->GetCurrentBackBufferIndex();
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
        std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
        commands->ResourceBarrier(1, &barrier);
        if (!Check(commands->Close(), "Close command list"))
        {
            return false;
        }
        ID3D12CommandList* lists[] = { commands.Get() };
        queue->ExecuteCommandLists(1, lists);
        const HRESULT present = swapChain->Present(1, 0);
        // 次のフレームでアロケーターを再利用する前に GPU の完了を確認します。
        if (!WaitForGpu() || !Check(present, "Present"))
        {
            return false;
        }
        if (present == DXGI_STATUS_OCCLUDED)
        {
            occluded = true;
            Sleep(50);
        }
        return true;
    }
}
