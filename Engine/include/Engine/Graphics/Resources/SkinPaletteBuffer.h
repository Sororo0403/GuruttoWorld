#pragma once
#include <iterator>
#include <Engine/Animation/SkinMatrix.h>
#include <Engine/Graphics/Resources/RenderFrameContext.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <array>
#include <map>
#include <memory>
#include <span>
#include <vector>

namespace Engine
{
    struct SkinPaletteView
    {
        ID3D12Resource* resource = nullptr;
        D3D12_CPU_DESCRIPTOR_HANDLE srv{};
    };
    class SkinPaletteBuffer final
    {
    public:
        static constexpr size_t Capacity = 257;
        /// <summary>現在のフレームへ不変のボーン行列を記録し、影・通常描画で共有します。</summary>
        SkinPaletteView Prepare(ID3D12Device* device, ID3D12GraphicsCommandList* commands,
                                std::span<const SkinMatrix> palette);

    private:
        struct Entry
        {
            Microsoft::WRL::ComPtr<ID3D12Resource> resource;
            Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> descriptor;
            std::vector<SkinMatrix> palette;
            /// <summary>再配置しないボーン領域とCPUのSRVを作成します。</summary>
            bool Initialize(ID3D12Device* device);
            /// <summary>寿命を保持したボーン領域のビューを取得します。</summary>
            SkinPaletteView View() const
            {
                return {resource.Get(), descriptor->GetCPUDescriptorHandleForHeapStart()};
            }
        };
        struct Frame
        {
            UINT64 serial = std::numeric_limits<UINT64>::max();
            size_t used = 0;
            std::multimap<UINT64, size_t> indices;
            std::vector<std::unique_ptr<Entry>> entries;
        };
        std::array<Frame, RenderFrameContext::SlotCount> frames_;
    };
}
