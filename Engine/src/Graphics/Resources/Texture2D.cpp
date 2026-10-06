#include <Engine/Graphics/Resources/Texture2D.h>
#include <Engine/Core/Log.h>
#include <Engine/Graphics/DirectX12/GpuSynchronization.h>

#include <wincodec.h>
#include <cstring>
#include <format>
#include <limits>
#include <memory>

#pragma comment(lib, "Windowscodecs.lib")
#pragma comment(lib, "Ole32.lib")

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

    struct ComApartment
    {
        HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        ~ComApartment()
        {
            if (SUCCEEDED(result))
            {
                CoUninitialize();
            }
        }
    };
}

namespace Engine
{
    bool Texture2D::Initialize(ID3D12Device* device, ID3D12CommandQueue* queue, const std::filesystem::path& path)
    {
        if (resource_ || device == nullptr || queue == nullptr || queue->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT)
        {
            return false;
        }
        ImageData image;
        ComPtr<ID3D12Resource> upload;
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
        if (path.empty())
        {
            image = { 1, 1, { 255, 255, 255, 255 } };
        }
        else if (!LoadImage(path, image))
        {
            return false;
        }
        if (!CreateResource(device, image) ||
            !CreateUploadBuffer(device, image, upload, footprint) ||
            !CreateShaderResourceView(device) || !UploadAndWait(device, queue, upload.Get(), footprint))
        {
            descriptorHeap_.Reset();
            cpuDescriptorHeap_.Reset();
            resource_.Reset();
            return false;
        }
        // 転送完了後なので、関数を抜ける際にアップロードバッファーを解放できます。
        Log::Info(std::format("Texture loaded: {}x{}", image.width, image.height));
        return true;
    }

    bool Texture2D::LoadImage(const std::filesystem::path& path, ImageData& image)
    {
        ComApartment apartment;
        if (apartment.result != RPC_E_CHANGED_MODE && !Check(apartment.result, "Initialize WIC COM apartment"))
        {
            return false;
        }
        ComPtr<IWICImagingFactory> factory;
        ComPtr<IWICBitmapDecoder> decoder;
        ComPtr<IWICBitmapFrameDecode> frame;
        ComPtr<IWICFormatConverter> converter;
        if (!Check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                IID_PPV_ARGS(&factory)), "Create WIC factory") ||
            !Check(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                WICDecodeMetadataCacheOnLoad, &decoder), "Open texture image") ||
            !Check(decoder->GetFrame(0, &frame), "Decode texture frame") ||
            !Check(frame->GetSize(&image.width, &image.height), "Get texture size"))
        {
            return false;
        }
        if (image.width == 0 || image.height == 0 ||
            image.width > D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION || image.height > D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION)
        {
            Log::Error("Unsupported texture dimensions.");
            return false;
        }
        const UINT64 size = static_cast<UINT64>(image.width) * image.height * 4;
        if (size > (std::numeric_limits<UINT>::max)())
        {
            Log::Error("Texture pixel data is too large.");
            return false;
        }
        if (!Check(factory->CreateFormatConverter(&converter), "Create WIC converter") ||
            !Check(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone,
                nullptr, 0.0, WICBitmapPaletteTypeCustom), "Convert texture to RGBA8"))
        {
            return false;
        }
        image.pixels.resize(static_cast<size_t>(size));
        return Check(converter->CopyPixels(nullptr, image.width * 4, static_cast<UINT>(size), image.pixels.data()),
            "Read texture pixels");
    }

    bool Texture2D::CreateResource(ID3D12Device* device, const ImageData& image)
    {
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        heap.CreationNodeMask = 1;
        heap.VisibleNodeMask = 1;
        D3D12_RESOURCE_DESC description{};
        description.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        description.Width = image.width;
        description.Height = image.height;
        description.DepthOrArraySize = 1;
        description.MipLevels = 1;
        description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        description.SampleDesc.Count = 1;
        return Check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &description,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&resource_)), "Create texture resource");
    }

    bool Texture2D::CreateUploadBuffer(ID3D12Device* device, const ImageData& image,
        ComPtr<ID3D12Resource>& upload, D3D12_PLACED_SUBRESOURCE_FOOTPRINT& footprint)
    {
        const auto textureDescription = resource_->GetDesc();
        UINT64 size = 0;
        device->GetCopyableFootprints(&textureDescription, 0, 1, 0, &footprint, nullptr, nullptr, &size);
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_UPLOAD;
        heap.CreationNodeMask = 1;
        heap.VisibleNodeMask = 1;
        D3D12_RESOURCE_DESC description{};
        description.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        description.Width = size;
        description.Height = 1;
        description.DepthOrArraySize = 1;
        description.MipLevels = 1;
        description.SampleDesc.Count = 1;
        description.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (!Check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &description,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upload)), "Create texture upload buffer"))
        {
            return false;
        }
        void* mapped = nullptr;
        const D3D12_RANGE readRange{ 0, 0 };
        if (!Check(upload->Map(0, &readRange, &mapped), "Map texture upload buffer"))
        {
            return false;
        }
        const size_t sourcePitch = static_cast<size_t>(image.width) * 4;
        for (UINT row = 0; row < image.height; ++row)
        {
            std::memcpy(static_cast<unsigned char*>(mapped) + footprint.Offset +
                static_cast<size_t>(row) * footprint.Footprint.RowPitch,
                image.pixels.data() + static_cast<size_t>(row) * sourcePitch, sourcePitch);
        }
        const D3D12_RANGE writtenRange{ 0, static_cast<SIZE_T>(size) };
        upload->Unmap(0, &writtenRange);
        return true;
    }

    bool Texture2D::UploadAndWait(ID3D12Device* device, ID3D12CommandQueue* queue, ID3D12Resource* upload,
        const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& footprint)
    {
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12GraphicsCommandList> commands;
        ComPtr<ID3D12Fence> fence;
        std::unique_ptr<void, decltype(&CloseHandle)> event(CreateEventW(nullptr, FALSE, FALSE, nullptr), CloseHandle);
        if (!event)
        {
            Log::Error("Create texture upload event failed.");
            return false;
        }
        if (!Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)),
                "Create texture upload allocator") ||
            !Check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr,
                IID_PPV_ARGS(&commands)), "Create texture upload commands") ||
            !Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)), "Create texture upload fence"))
        {
            return false;
        }
        D3D12_TEXTURE_COPY_LOCATION source{};
        source.pResource = upload;
        source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        source.PlacedFootprint = footprint;
        D3D12_TEXTURE_COPY_LOCATION destination{};
        destination.pResource = resource_.Get();
        destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        commands->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = resource_.Get();
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        commands->ResourceBarrier(1, &barrier);
        if (!Check(commands->Close(), "Close texture upload commands"))
        {
            return false;
        }
        ID3D12CommandList* lists[] = { commands.Get() };
        queue->ExecuteCommandLists(1, lists);
        return SignalGpuFence(device, queue, fence.Get(), 1) &&
            WaitForGpuFence(device, fence.Get(), 1, event.get());
    }

    bool Texture2D::CreateShaderResourceView(ID3D12Device* device)
    {
        D3D12_DESCRIPTOR_HEAP_DESC description{};
        description.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        description.NumDescriptors = 1;
        description.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (!Check(device->CreateDescriptorHeap(&description, IID_PPV_ARGS(&descriptorHeap_)), "Create texture SRV heap"))
        {
            return false;
        }
        D3D12_SHADER_RESOURCE_VIEW_DESC view{};
        view.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        view.Texture2D.MipLevels = 1;
        device->CreateShaderResourceView(resource_.Get(), &view, descriptorHeap_->GetCPUDescriptorHandleForHeapStart());
        description.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        if (!Check(device->CreateDescriptorHeap(&description, IID_PPV_ARGS(&cpuDescriptorHeap_)), "Create copyable texture SRV heap")) return false;
        device->CreateShaderResourceView(resource_.Get(), &view, cpuDescriptorHeap_->GetCPUDescriptorHandleForHeapStart());
        return true;
    }

    ID3D12DescriptorHeap* Texture2D::GetDescriptorHeap() const noexcept
    {
        return descriptorHeap_.Get();
    }

    D3D12_GPU_DESCRIPTOR_HANDLE Texture2D::GetGpuHandle() const noexcept
    {
        return descriptorHeap_ ? descriptorHeap_->GetGPUDescriptorHandleForHeapStart() : D3D12_GPU_DESCRIPTOR_HANDLE{};
    }
}
