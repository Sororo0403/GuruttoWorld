#pragma once
#include <Engine/Animation/SkinMatrix.h>
#include <Engine/Graphics/Resources/RenderFrameContext.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <memory>
#include <span>
#include <vector>

namespace Engine
{
    struct SkinPaletteView { ID3D12Resource* resource=nullptr; D3D12_CPU_DESCRIPTOR_HANDLE srv{}; };
    class SkinPaletteBuffer final
    {
    public:
        static constexpr size_t Capacity=257;
        /// <summary>現在のフレームへ不変のボーン行列を記録し、影・通常描画で共有します。</summary>
        SkinPaletteView Prepare(ID3D12Device* device,ID3D12GraphicsCommandList* commands,std::span<const SkinMatrix> palette)
        {
            RenderFrameContext context;
            if (!device || palette.empty() || palette.size()>Capacity || !RenderFrameContext::Current(commands,context)) return {};
            for (const auto& matrix : palette)
                for (const auto* value : {&matrix.position,&matrix.normal})
                    for (const auto& row : value->m) for (const float entry : row) if (!std::isfinite(entry)) return {};
            auto& frame=frames_[context.slot];
            if (frame.serial!=context.serial) { frame.serial=context.serial; frame.used=0; frame.indices.clear(); }
            UINT64 hash=14695981039346656037ULL;
            for (const auto byte : std::as_bytes(palette)) { hash^=std::to_integer<unsigned char>(byte); hash*=1099511628211ULL; }
            const auto [first,last]=frame.indices.equal_range(hash);
            for (auto candidate=first;candidate!=last;++candidate)
                if (std::ranges::equal(frame.entries[candidate->second]->palette,palette)) return frame.entries[candidate->second]->View();
            if (frame.used>=1024) return {};
            if (frame.used==frame.entries.size())
            {
                auto entry=std::make_unique<Entry>(); if (!entry->Initialize(device)) return {};
                frame.entries.push_back(std::move(entry));
            }
            auto& entry=*frame.entries[frame.used]; void* destination=nullptr; const D3D12_RANGE read{0,0};
            if (FAILED(entry.resource->Map(0,&read,&destination))) return {};
            std::memcpy(destination,palette.data(),palette.size_bytes());
            const D3D12_RANGE written{0,palette.size_bytes()}; entry.resource->Unmap(0,&written);
            entry.palette.assign(palette.begin(),palette.end()); frame.indices.emplace(hash,frame.used++); return entry.View();
        }
    private:
        struct Entry
        {
            Microsoft::WRL::ComPtr<ID3D12Resource> resource;
            Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> descriptor;
            std::vector<SkinMatrix> palette;
            /// <summary>再配置しないボーン領域とCPUのSRVを作成します。</summary>
            bool Initialize(ID3D12Device* device)
            {
                D3D12_HEAP_PROPERTIES heap{}; heap.Type=D3D12_HEAP_TYPE_UPLOAD;
                D3D12_RESOURCE_DESC buffer{}; buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;
                buffer.Width=Capacity*sizeof(SkinMatrix); buffer.Height=1; buffer.DepthOrArraySize=buffer.MipLevels=1;
                buffer.SampleDesc.Count=1; buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
                if (FAILED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&resource)))) return false;
                D3D12_DESCRIPTOR_HEAP_DESC description{}; description.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; description.NumDescriptors=1;
                if (FAILED(device->CreateDescriptorHeap(&description,IID_PPV_ARGS(&descriptor)))) return false;
                D3D12_SHADER_RESOURCE_VIEW_DESC view{}; view.ViewDimension=D3D12_SRV_DIMENSION_BUFFER;
                view.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                view.Buffer.NumElements=static_cast<UINT>(Capacity); view.Buffer.StructureByteStride=sizeof(SkinMatrix);
                device->CreateShaderResourceView(resource.Get(),&view,descriptor->GetCPUDescriptorHandleForHeapStart()); return true;
            }
            /// <summary>寿命を保持したボーン領域のビューを取得します。</summary>
            SkinPaletteView View() const { return {resource.Get(),descriptor->GetCPUDescriptorHandleForHeapStart()}; }
        };
        struct Frame
        {
            UINT64 serial=std::numeric_limits<UINT64>::max(); size_t used=0;
            std::multimap<UINT64,size_t> indices;
            std::vector<std::unique_ptr<Entry>> entries;
        };
        std::array<Frame,RenderFrameContext::SlotCount> frames_;
    };
}
