#pragma once
#include <Engine/Graphics/ShaderCompiler.h>
#include <Engine/Graphics/Resources/RenderFrameContext.h>
#include <Engine/Graphics/Resources/DepthBuffer.h>
#include <DirectXCollision.h>
#include <wrl/client.h>
#include <limits>
#include <array>
#include <memory>
#include <vector>

namespace Engine
{
    class OcclusionRenderer final
    {
    public:
        /// <summary>色と深度を変更しない境界箱のGPU遮蔽判定パイプラインを生成します。</summary>
        bool Initialize(ID3D12Device* device, const std::filesystem::path& shader);
        /// <summary>現フレームの境界箱を深度へ照合し、後続の描画をGPUの結果で条件化します。</summary>
        bool Begin(ID3D12GraphicsCommandList* commands, const DirectX::BoundingBox& bounds,
                   const DirectX::XMFLOAT4X4& world, const DirectX::XMFLOAT4X4& viewProjection);
        /// <summary>次の物体やUIへ判定を持ち越さないよう条件付き描画を解除します。</summary>
        static void End(ID3D12GraphicsCommandList* commands)
        {
            commands->SetPredication(nullptr, 0, D3D12_PREDICATION_OP_EQUAL_ZERO);
        }

    private:
        /// <summary>判定領域の書き込みとGPU条件参照を同期します。</summary>
        static void Transition(ID3D12GraphicsCommandList* commands, ID3D12Resource* resource,
                               D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after);
        struct Entry
        {
            Microsoft::WRL::ComPtr<ID3D12QueryHeap> query;
            Microsoft::WRL::ComPtr<ID3D12Resource> buffer;
            bool predication = false;
            /// <summary>フレームのフェンス完了後だけ再利用するクエリと結果領域を作成します。</summary>
            bool Initialize(ID3D12Device* device);
        };
        struct Frame
        {
            UINT64 serial = (std::numeric_limits<UINT64>::max)();
            size_t used = 0;
            std::vector<std::unique_ptr<Entry>> entries;
        };
        std::array<Frame, RenderFrameContext::SlotCount> frames_;
        Microsoft::WRL::ComPtr<ID3D12Device> device_;
        Microsoft::WRL::ComPtr<ID3D12RootSignature> root_;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline_;
    };
}
