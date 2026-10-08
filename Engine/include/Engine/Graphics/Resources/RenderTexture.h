#pragma once
#include <Engine/Graphics/Resources/DepthBuffer.h>
#include <array>
#include <memory>

namespace Engine
{
    class DirectX12Renderer;
    // Color/depth target for a Scene view. All calls use the renderer's thread/queue.
    class RenderTexture final
    {
    public:
        static constexpr DXGI_FORMAT Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        static constexpr DXGI_FORMAT HdrFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
        RenderTexture();
        // The caller must wait for GPU completion before destruction.
        ~RenderTexture();
        RenderTexture(const RenderTexture&) = delete;
        RenderTexture& operator=(const RenderTexture&) = delete;
        // Call outside Render. Waits before replacing old GPU resources; failure preserves them.
        // Zero size (a hidden/minimized view) is rejected without releasing the last valid target.
        bool Resize(DirectX12Renderer& renderer, UINT width, UINT height,DXGI_FORMAT format=Format);
        /// <summary>未初期化の描画先を作成します。GPUが参照する既存領域は置換しません。</summary>
        bool Initialize(ID3D12Device* device,UINT width,UINT height,DXGI_FORMAT format=Format);
        // Pair Begin/End on the same submitted command list. End leaves color shader-readable.
        bool Begin(ID3D12GraphicsCommandList* commands, const std::array<float, 4>& clearColor);
        bool End(ID3D12GraphicsCommandList* commands);
        UINT GetWidth() const noexcept;
        UINT GetHeight() const noexcept;
        /// <summary>描画先の色形式を取得します。未初期化の場合はUNKNOWNを返します。</summary>
        DXGI_FORMAT GetFormat() const noexcept;
        ID3D12Resource* GetResource() const noexcept;
        // CPU SRV for copying into the UI's descriptor heap; never bind this heap to ImGui.
        D3D12_CPU_DESCRIPTOR_HANDLE GetShaderResourceView() const noexcept;
    private:
        struct Resources;
        static bool CreateColor(ID3D12Device* device, Resources& resources);
        static bool CreateViews(ID3D12Device* device, Resources& resources);
        void Transition(ID3D12GraphicsCommandList* commands, D3D12_RESOURCE_STATES before,
            D3D12_RESOURCE_STATES after);
        std::unique_ptr<Resources> resources_;
        ID3D12GraphicsCommandList* recording_ = nullptr;
    };
}
