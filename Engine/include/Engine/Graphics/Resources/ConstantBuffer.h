#pragma once
#include <Engine/Graphics/Resources/RenderFrameContext.h>
#include <wrl/client.h>
#include <array>
#include <vector>
#include <span>
#include <limits>
#include <cstring>

namespace Engine
{
    // 各描画のデータは、そのフレームスロットのフェンスが完了するまで変更しません。
    class ConstantBuffer final
    {
    public:
        /// <summary>使用する資源と状態を準備します。</summary>
        D3D12_GPU_VIRTUAL_ADDRESS Prepare(ID3D12Device* device, ID3D12GraphicsCommandList* commands,
                                          std::span<const float> values);

    private:
        struct Frame
        {
            UINT64 serial = std::numeric_limits<UINT64>::max();
            size_t used = 0;
            std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> buffers;
        };
        std::array<Frame, RenderFrameContext::SlotCount> frames_;
    };
}
