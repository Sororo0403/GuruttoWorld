#include <Engine/Graphics/DirectX12/GpuSynchronization.h>
#include <Engine/Core/Log.h>

#include <wrl/client.h>
#include <exception>
#include <format>
#include <limits>

namespace
{
    bool DeviceLost(ID3D12Device* device)
    {
        const HRESULT reason = device->GetDeviceRemovedReason();
        if (FAILED(reason))
        {
            Engine::Log::Error(std::format("GPU device lost: 0x{:08X}", static_cast<unsigned long>(reason)));
            return true;
        }
        return false;
    }

    bool StopDevice(ID3D12Device* device)
    {
        if (DeviceLost(device))
        {
            return false;
        }
        Microsoft::WRL::ComPtr<ID3D12Device5> removable;
        if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&removable))))
        {
            removable->RemoveDevice();
            if (DeviceLost(device))
            {
                return false;
            }
        }
        // GPU 完了もデバイス喪失も確認できない環境では、スタックを巻き戻して解放しません。
        Engine::Log::Error("Cannot safely release GPU resources. Terminating without unwinding.");
        std::terminate();
    }
}

namespace Engine
{
    bool SignalGpuFence(ID3D12Device* device, ID3D12CommandQueue* queue, ID3D12Fence* fence, UINT64 value)
    {
        const HRESULT result = queue->Signal(fence, value);
        if (FAILED(result))
        {
            Log::Error(std::format("GPU fence signal failed: 0x{:08X}", static_cast<unsigned long>(result)));
            return StopDevice(device);
        }
        return true;
    }

    bool WaitForGpuFence(ID3D12Device* device, ID3D12Fence* fence, UINT64 value,
        HANDLE event, DWORD timeoutMilliseconds)
    {
        const ULONGLONG started = GetTickCount64();
        bool registered = false;
        while (true)
        {
            const UINT64 completed = fence->GetCompletedValue();
            if (completed == (std::numeric_limits<UINT64>::max)())
            {
                return StopDevice(device);
            }
            if (completed >= value)
            {
                return !DeviceLost(device);
            }
            if (DeviceLost(device))
            {
                return false;
            }
            if (GetTickCount64() - started >= timeoutMilliseconds)
            {
                Log::Error("GPU fence wait timed out; stopping the device before releasing resources.");
                return StopDevice(device);
            }
            if (event != nullptr && !registered)
            {
                if (SUCCEEDED(fence->SetEventOnCompletion(value, event)))
                {
                    registered = true;
                }
                else
                {
                    Log::Warning("GPU fence event registration failed; polling completion instead.");
                    event = nullptr;
                }
            }
            if (event != nullptr)
            {
                const DWORD result = WaitForSingleObject(event, 10);
                if (result != WAIT_OBJECT_0 && result != WAIT_TIMEOUT)
                {
                    Log::Warning("GPU fence event wait failed; polling completion instead.");
                    event = nullptr;
                }
                // イベントが通知されても、次のループで実際の完了値を確認します。
            }
            else
            {
                Sleep(1);
            }
        }
    }
}
