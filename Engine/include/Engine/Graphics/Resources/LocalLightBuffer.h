#pragma once
#include <Engine/Graphics/Materials/DirectionalLight.h>
#include <Engine/Graphics/Resources/RenderFrameContext.h>
#include <wrl/client.h>
#include <algorithm>
#include <cstring>
#include <cmath>
#include <limits>
#include <memory>
#include <span>

namespace Engine
{
    struct LocalLightView { ID3D12Resource* resource=nullptr; D3D12_CPU_DESCRIPTOR_HANDLE srv{}; };
    class LocalLightBuffer final
    {
    public:
        static constexpr size_t Capacity=32;
        /// <summary>同一フレームの異なる照明を別領域へ記録し、使用中のGPUデータを保持します。</summary>
        LocalLightView Prepare(ID3D12Device* device,ID3D12GraphicsCommandList* commands,std::span<const LocalLight> lights)
        {
            RenderFrameContext context;
            if (!device || lights.empty() || lights.size()>Capacity || !RenderFrameContext::Current(commands,context)) return {};
            if (!std::ranges::all_of(lights,Valid)) return {};
            auto& frame=frames_[context.slot];
            if (frame.serial!=context.serial) { frame.serial=context.serial; frame.used=0; }
            for (size_t index=0;index<frame.used;++index)
                if (std::ranges::equal(frame.entries[index]->lights,lights)) return frame.entries[index]->View();
            if (frame.used>=1024) return {};
            if (frame.used==frame.entries.size())
            {
                auto entry=std::make_unique<Entry>();
                if (!entry->Initialize(device)) return {};
                frame.entries.push_back(std::move(entry));
            }
            auto& entry=*frame.entries[frame.used];
            Packet packet; packet.header[0]=static_cast<float>(lights.size());
            std::copy(lights.begin(),lights.end(),packet.lights.begin());
            void* data=nullptr; const D3D12_RANGE read{0,0};
            if (FAILED(entry.resource->Map(0,&read,&data))) return {};
            std::memcpy(data,&packet,sizeof(packet));
            const D3D12_RANGE written{0,sizeof(packet)}; entry.resource->Unmap(0,&written);
            entry.lights.assign(lights.begin(),lights.end()); ++frame.used;
            return entry.View();
        }
    private:
        /// <summary>GPU照明計算で不正な値やゼロの到達距離を使わないよう検証します。</summary>
        static bool Valid(const LocalLight& light)
        {
            const auto finite=[](float value) { return std::isfinite(value); };
            const auto color=[](float value) { return std::isfinite(value) && value>=0 && value<=1; };
            return std::ranges::all_of(light.position,finite) && std::ranges::all_of(light.direction,finite) &&
                std::ranges::all_of(light.color,color) && finite(light.intensity) && light.intensity>=0 &&
                finite(light.range) && light.range>0 && (light.spot==0 || light.spot==1) &&
                (light.spot==0 || (finite(light.innerCosine) && finite(light.outerCosine) && light.innerCosine<=1 &&
                    light.outerCosine>=-1 && light.innerCosine>light.outerCosine));
        }
        struct Packet { std::array<float,4> header{}; std::array<LocalLight,Capacity> lights{}; };
        static_assert(sizeof(Packet)==16+64*Capacity);
        struct Entry
        {
            Microsoft::WRL::ComPtr<ID3D12Resource> resource;
            Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> descriptor;
            std::vector<LocalLight> lights;
            /// <summary>フレーム枠に属するアップロード領域と不変のSRVを作成します。</summary>
            bool Initialize(ID3D12Device* device)
            {
                D3D12_HEAP_PROPERTIES heap{}; heap.Type=D3D12_HEAP_TYPE_UPLOAD;
                D3D12_RESOURCE_DESC description{}; description.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;
                description.Width=sizeof(Packet); description.Height=1; description.DepthOrArraySize=1;
                description.MipLevels=1; description.SampleDesc.Count=1; description.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
                if (FAILED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&description,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&resource)))) return false;
                D3D12_DESCRIPTOR_HEAP_DESC descriptors{}; descriptors.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; descriptors.NumDescriptors=1;
                if (FAILED(device->CreateDescriptorHeap(&descriptors,IID_PPV_ARGS(&descriptor)))) return false;
                D3D12_SHADER_RESOURCE_VIEW_DESC view{}; view.ViewDimension=D3D12_SRV_DIMENSION_BUFFER;
                view.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                view.Buffer.NumElements=1+static_cast<UINT>(Capacity)*4; view.Buffer.StructureByteStride=16;
                device->CreateShaderResourceView(resource.Get(),&view,descriptor->GetCPUDescriptorHandleForHeapStart()); return true;
            }
            /// <summary>寿命を保持した照明領域のビューを返します。</summary>
            LocalLightView View() const { return {resource.Get(),descriptor->GetCPUDescriptorHandleForHeapStart()}; }
        };
        struct Frame
        {
            std::uint64_t serial=std::numeric_limits<std::uint64_t>::max();
            size_t used=0;
            std::vector<std::unique_ptr<Entry>> entries;
        };
        std::array<Frame,RenderFrameContext::SlotCount> frames_;
    };
}
