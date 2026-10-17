#include <Engine/Graphics/Resources/Texture2D.h>
#include <Engine/Core/Log.h>
#include <Engine/Assets/AssetDatabase.h>
#include <Engine/Graphics/DirectX12/GpuSynchronization.h>
#include <Engine/Graphics/DirectX12/GpuRetirement.h>

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

    bool ValidPixelUpload(const ID3D12Device* device, ID3D12CommandQueue* queue, UINT width, UINT height,
                          const std::vector<unsigned char>& pixels)
    {
        if (!device || !queue || queue->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT || !width || !height ||
            width > 16384 || height > 16384 || pixels.size() != size_t(width) * height * 4)
            return false;
        ComPtr<ID3D12Device> queueDevice;
        if (FAILED(queue->GetDevice(IID_PPV_ARGS(&queueDevice))) || queueDevice.Get() != device)
            return false;
        return true;
    }
    void RecordTextureCopy(ID3D12GraphicsCommandList* commands, ID3D12Resource* upload,
                           ID3D12Resource* destinationResource,
                           const std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT>& footprints)
    {
        D3D12_TEXTURE_COPY_LOCATION source{};
        source.pResource = upload;
        source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        D3D12_TEXTURE_COPY_LOCATION destination{};
        destination.pResource = destinationResource;
        destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        for (size_t index = 0; index < footprints.size(); ++index)
        {
            source.PlacedFootprint = footprints[index];
            destination.SubresourceIndex = static_cast<UINT>(index);
            commands->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
        }
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = destinationResource;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        commands->ResourceBarrier(1, &barrier);
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
        if (resource_ || device == nullptr || queue == nullptr ||
            queue->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT)
        {
            return false;
        }
        ImageData image;
        if (path.empty())
        {
            image = {1, 1, {255, 255, 255, 255}};
        }
        else if (!LoadImage(path, image))
        {
            return false;
        }
        try
        {
            return InitializePixels(device, queue, image.width, image.height, std::move(image.pixels),
                                    path.empty() ? TextureImportSettings{} : AssetDatabase::Read(path).texture);
        }
        catch (const std::exception& error)
        {
            Log::Error(error.what());
            return false;
        }
    }

    bool Texture2D::InitializePixels(ID3D12Device* device, ID3D12CommandQueue* queue, UINT width, UINT height,
                                     std::vector<unsigned char> pixels, const TextureImportSettings& settings,
                                     bool deferUpload)
    {
        if (resource_ || !ValidPixelUpload(device, queue, width, height, pixels))
            return false;
        std::vector<TextureLevel> levels;
        try
        {
            levels = TextureImport::Build({width, height, std::move(pixels)}, settings);
        }
        catch (const std::exception& error)
        {
            Log::Error(error.what());
            return false;
        }
        ComPtr<ID3D12Resource> upload;
        std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> footprints(levels.size());
        if (!CreateResource(device, levels, TextureImport::Compressed(settings, levels.front())) ||
            !CreateUploadBuffer(device, levels, upload, footprints) || !CreateShaderResourceView(device) ||
            !UploadAndWait(device, queue, upload.Get(), footprints, deferUpload))
        {
            descriptorHeap_.Reset();
            cpuDescriptorHeap_.Reset();
            resource_.Reset();
            return false;
        }
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
        if (!Check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)),
                   "Create WIC factory") ||
            !Check(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad,
                                                      &decoder),
                   "Open texture image") ||
            !Check(decoder->GetFrame(0, &frame), "Decode texture frame") ||
            !Check(frame->GetSize(&image.width, &image.height), "Get texture size"))
        {
            return false;
        }
        if (image.width == 0 || image.height == 0 || image.width > D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION ||
            image.height > D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION)
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
            !Check(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr,
                                         0.0, WICBitmapPaletteTypeCustom),
                   "Convert texture to RGBA8"))
        {
            return false;
        }
        image.pixels.resize(static_cast<size_t>(size));
        return Check(converter->CopyPixels(nullptr, image.width * 4, static_cast<UINT>(size), image.pixels.data()),
                     "Read texture pixels");
    }

    bool Texture2D::CreateResource(ID3D12Device* device, const std::vector<TextureLevel>& levels, bool compressed)
    {
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        heap.CreationNodeMask = 1;
        heap.VisibleNodeMask = 1;
        D3D12_RESOURCE_DESC description{};
        description.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        description.Width = levels.front().width;
        description.Height = levels.front().height;
        description.DepthOrArraySize = 1;
        description.MipLevels = static_cast<UINT16>(levels.size());
        description.Format = compressed ? DXGI_FORMAT_BC3_UNORM : DXGI_FORMAT_R8G8B8A8_UNORM;
        description.SampleDesc.Count = 1;
        return Check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &description,
                                                     D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&resource_)),
                     "Create texture resource");
    }

    bool Texture2D::CreateUploadBuffer(ID3D12Device* device, const std::vector<TextureLevel>& levels,
                                       ComPtr<ID3D12Resource>& upload,
                                       std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT>& footprints)
    {
        const auto textureDescription = resource_->GetDesc();
        UINT64 size = 0;
        std::vector<UINT> rows(levels.size());
        std::vector<UINT64> pitches(levels.size());
        device->GetCopyableFootprints(&textureDescription, 0, static_cast<UINT>(levels.size()), 0, footprints.data(),
                                      rows.data(), pitches.data(), &size);
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
                                                   D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upload)),
                   "Create texture upload buffer"))
        {
            return false;
        }
        void* mapped = nullptr;
        const D3D12_RANGE readRange{0, 0};
        if (!Check(upload->Map(0, &readRange, &mapped), "Map texture upload buffer"))
        {
            return false;
        }
        for (size_t index = 0; index < levels.size(); ++index)
        {
            const auto& footprint = footprints[index];
            for (UINT row = 0; row < rows[index]; ++row)
                std::memcpy(static_cast<unsigned char*>(mapped) + footprint.Offset +
                                static_cast<size_t>(row) * footprint.Footprint.RowPitch,
                            levels[index].pixels.data() +
                                static_cast<size_t>(row) * static_cast<size_t>(pitches[index]),
                            static_cast<size_t>(pitches[index]));
        }
        const D3D12_RANGE writtenRange{0, static_cast<SIZE_T>(size)};
        upload->Unmap(0, &writtenRange);
        return true;
    }

    bool Texture2D::UploadAndWait(ID3D12Device* device, ID3D12CommandQueue* queue, ID3D12Resource* upload,
                                  const std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT>& footprints, bool deferUpload)
    {
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12GraphicsCommandList> commands;
        ComPtr<ID3D12Fence> fence;
        std::unique_ptr<void, decltype(&CloseHandle)> event(
            deferUpload ? nullptr : CreateEventW(nullptr, FALSE, FALSE, nullptr), CloseHandle);
        if (!deferUpload && !event)
        {
            Log::Error("Create texture upload event failed.");
            return false;
        }
        if (!Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)),
                   "Create texture upload allocator") ||
            !Check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr,
                                             IID_PPV_ARGS(&commands)),
                   "Create texture upload commands") ||
            !Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)), "Create texture upload fence"))
        {
            return false;
        }
        RecordTextureCopy(commands.Get(), upload, resource_.Get(), footprints);
        if (!Check(commands->Close(), "Close texture upload commands"))
        {
            return false;
        }
        struct UploadResources
        {
            ComPtr<ID3D12CommandAllocator> allocator;
            ComPtr<ID3D12GraphicsCommandList> commands;
            ComPtr<ID3D12Resource> upload, destination;
            ComPtr<ID3D12DescriptorHeap> descriptors, cpuDescriptors;
        };
        std::shared_ptr<GpuRetirement> retirement;
        std::shared_ptr<UploadResources> retained;
        if (deferUpload)
        {
            // 送信前にすべての割り当てを完了します。復元処理でこの Texture2D が直ちに破棄される可能性があります。
            retirement = GpuRetirement::Dedicated(device, queue);
            if (!retirement)
                return false;
            retained = std::make_shared<UploadResources>();
            retained->allocator = allocator;
            retained->commands = commands;
            retained->upload = upload;
            retained->destination = resource_;
            retained->descriptors = descriptorHeap_;
            retained->cpuDescriptors = cpuDescriptorHeap_;
        }
        ID3D12CommandList* lists[] = {commands.Get()};
        queue->ExecuteCommandLists(1, lists);
        if (deferUpload)
        {
            try
            {
                // Retire が false を返すのは、SignalGpuFence がデバイス喪失を確認した場合だけです。確認できなければプロセスを終了します。
                if (!retirement->Retire(retained))
                    return false;
                uploadRetirement_ = std::move(retirement);
                return true;
            }
            catch (...)
            {
                // 代替のフェンスは作成済みです。Retire 内の割り当て失敗でも、処理中のリソースを解放してはいけません。
                if (SignalGpuFence(device, queue, fence.Get(), 1))
                    WaitForGpuFence(device, fence.Get(), 1, nullptr);
                throw;
            }
        }
        return SignalGpuFence(device, queue, fence.Get(), 1) && WaitForGpuFence(device, fence.Get(), 1, event.get());
    }

    bool Texture2D::CreateShaderResourceView(ID3D12Device* device)
    {
        D3D12_DESCRIPTOR_HEAP_DESC description{};
        description.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        description.NumDescriptors = 1;
        description.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (!Check(device->CreateDescriptorHeap(&description, IID_PPV_ARGS(&descriptorHeap_)),
                   "Create texture SRV heap"))
        {
            return false;
        }
        D3D12_SHADER_RESOURCE_VIEW_DESC view{};
        view.Format = resource_->GetDesc().Format;
        view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        view.Texture2D.MipLevels = resource_->GetDesc().MipLevels;
        device->CreateShaderResourceView(resource_.Get(), &view, descriptorHeap_->GetCPUDescriptorHandleForHeapStart());
        description.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        if (!Check(device->CreateDescriptorHeap(&description, IID_PPV_ARGS(&cpuDescriptorHeap_)),
                   "Create copyable texture SRV heap"))
            return false;
        device->CreateShaderResourceView(resource_.Get(), &view,
                                         cpuDescriptorHeap_->GetCPUDescriptorHandleForHeapStart());
        return true;
    }

    ID3D12DescriptorHeap* Texture2D::GetDescriptorHeap() const noexcept
    {
        if (uploadRetirement_)
            uploadRetirement_->Collect();
        return descriptorHeap_.Get();
    }

    D3D12_GPU_DESCRIPTOR_HANDLE Texture2D::GetGpuHandle() const noexcept
    {
        return descriptorHeap_ ? descriptorHeap_->GetGPUDescriptorHandleForHeapStart() : D3D12_GPU_DESCRIPTOR_HANDLE{};
    }
}
