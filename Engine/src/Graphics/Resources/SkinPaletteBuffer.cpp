#include <Engine/Graphics/Resources/SkinPaletteBuffer.h>

namespace Engine
{
    auto SkinPaletteBuffer::Prepare(ID3D12Device* device, ID3D12GraphicsCommandList* commands,
                                    std::span<const SkinMatrix> palette) -> SkinPaletteView
    {
        RenderFrameContext context;
        if (!device || palette.empty() || palette.size() > Capacity || !RenderFrameContext::Current(commands, context))
            return {};
        if (std::any_of(palette.begin(), palette.end(),
                        [](const auto& matrix)
                        {
                            return !FiniteMatrix(matrix.position) || !FiniteMatrix(matrix.normal);
                        }))
            return {};
        auto& frame = frames_[context.slot];
        if (frame.serial != context.serial)
        {
            frame.serial = context.serial;
            frame.used = 0;
            frame.indices.clear();
        }
        UINT64 hash = 14695981039346656037ULL;
        for (const auto byte : std::as_bytes(palette))
        {
            hash ^= std::to_integer<unsigned char>(byte);
            hash *= 1099511628211ULL;
        }
        const auto [first, last] = frame.indices.equal_range(hash);
        for (auto candidate = first; candidate != last; ++candidate)
            if (std::ranges::equal(frame.entries[candidate->second]->palette, palette))
                return frame.entries[candidate->second]->View();
        if (frame.used >= 1024)
            return {};
        if (frame.used == frame.entries.size())
        {
            auto entry = std::make_unique<Entry>();
            if (!entry->Initialize(device))
                return {};
            frame.entries.push_back(std::move(entry));
        }
        auto& entry = *frame.entries[frame.used];
        void* destination = nullptr;
        const D3D12_RANGE read{0, 0};
        if (FAILED(entry.resource->Map(0, &read, &destination)))
            return {};
        std::memcpy(destination, palette.data(), palette.size_bytes());
        const D3D12_RANGE written{0, palette.size_bytes()};
        entry.resource->Unmap(0, &written);
        entry.palette.assign(palette.begin(), palette.end());
        frame.indices.emplace(hash, frame.used++);
        return entry.View();
    }

    auto SkinPaletteBuffer::Entry::Initialize(ID3D12Device* device) -> bool
    {
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC buffer{};
        buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        buffer.Width = Capacity * sizeof(SkinMatrix);
        buffer.Height = 1;
        buffer.DepthOrArraySize = buffer.MipLevels = 1;
        buffer.SampleDesc.Count = 1;
        buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer,
                                                   D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                   IID_PPV_ARGS(&resource))))
            return false;
        D3D12_DESCRIPTOR_HEAP_DESC description{};
        description.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        description.NumDescriptors = 1;
        if (FAILED(device->CreateDescriptorHeap(&description, IID_PPV_ARGS(&descriptor))))
            return false;
        D3D12_SHADER_RESOURCE_VIEW_DESC view{};
        view.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        view.Buffer.NumElements = static_cast<UINT>(Capacity);
        view.Buffer.StructureByteStride = sizeof(SkinMatrix);
        device->CreateShaderResourceView(resource.Get(), &view, descriptor->GetCPUDescriptorHandleForHeapStart());
        return true;
    }
}
