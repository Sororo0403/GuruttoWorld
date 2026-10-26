#pragma once
#include <Engine/Graphics/Materials/DirectionalLight.h>
#include <Engine/Graphics/Resources/RenderFrameContext.h>
#include <Engine/Graphics/Renderers/ShadowMap.h>
#include <wrl/client.h>
#include <Engine/Core/Validation.h>
#include <algorithm>
#include <cstring>
#include <cmath>
#include <limits>
#include <memory>
#include <span>

namespace Engine
{
    struct LocalLightView
    {
        ID3D12Resource* resource = nullptr;
        D3D12_CPU_DESCRIPTOR_HANDLE srv{};
    };
    class LocalLightBuffer final
    {
    public:
        static constexpr size_t Capacity = 32;
        /// <summary>同一フレームの異なる照明を別領域へ記録し、使用中のGPUデータを保持します。</summary>
        LocalLightView Prepare(ID3D12Device* device, ID3D12GraphicsCommandList* commands,
                               std::span<const LocalLight> lights, const ShadowMap* shadows = nullptr,
                               std::array<float, 2> environment = {});

    private:
        /// <summary>資源の準備に必要な入力条件を検証します。</summary>
        static bool ValidRequest(const ID3D12Device* device, ID3D12GraphicsCommandList* commands,
                                 std::span<const LocalLight> lights, std::array<float, 2> environment,
                                 RenderFrameContext& context);
        /// <summary>GPU照明計算で不正な値やゼロの到達距離を使わないよう検証します。</summary>
        static bool Valid(const LocalLight& light);
        /// <summary>スポット光の角度範囲を検証します。</summary>
        static bool ValidCone(const LocalLight& light);
        /// <summary>局所影のアトラス範囲と面数を検証します。</summary>
        static bool ValidShadow(const LocalLight& light);
        struct Packet
        {
            std::array<float, 4> header{};
            std::array<LocalLight, Capacity> lights{};
            std::array<DirectX::XMFLOAT4X4, ShadowMap::LocalFaces> matrices{};
        };
        static_assert(sizeof(Packet) == 16 + 64 * Capacity + 64 * ShadowMap::LocalFaces);
        struct Entry
        {
            Microsoft::WRL::ComPtr<ID3D12Resource> resource;
            Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> descriptor;
            std::vector<LocalLight> lights;
            bool shadowed = false;
            std::array<float, 2> environment{};
            /// <summary>フレーム枠に属するアップロード領域と不変のSRVを作成します。</summary>
            bool Initialize(ID3D12Device* device);
            /// <summary>寿命を保持した照明領域のビューを返します。</summary>
            LocalLightView View() const
            {
                return {resource.Get(), descriptor->GetCPUDescriptorHandleForHeapStart()};
            }
        };
        struct Frame
        {
            std::uint64_t serial = std::numeric_limits<std::uint64_t>::max();
            size_t used = 0;
            std::vector<std::unique_ptr<Entry>> entries;
        };
        /// <summary>準備済みのデータをGPUへ転送します。</summary>
        static bool Upload(Entry& entry, std::span<const LocalLight> lights, const ShadowMap* shadows,
                           std::array<float, 2> environment);
        std::array<Frame, RenderFrameContext::SlotCount> frames_;
    };
}
