#pragma once
#include <Engine/Graphics/Resources/DepthBuffer.h>
#include <array>
#include <memory>

namespace Engine
{
    class DirectX12Renderer;
    // Scene ビューのカラー・深度ターゲットです。すべての呼び出しでレンダラーのスレッドとキューを使います。
    class RenderTexture final
    {
    public:
        static constexpr DXGI_FORMAT Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        static constexpr DXGI_FORMAT HdrFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
        RenderTexture();
        /// <summary>
        /// 破棄前に呼び出し側で GPU の完了を待ってください。
        /// </summary>
        ~RenderTexture();
        RenderTexture(const RenderTexture&) = delete;
        RenderTexture& operator=(const RenderTexture&) = delete;
        /// <summary>
        /// Render の外で呼んでください。古い GPU リソースの置き換え前に完了を待ち、失敗時は元のリソースを保持します。
        /// 非表示・最小化によるサイズ 0 は、直前の有効なターゲットを解放せずに拒否します。
        /// </summary>
        bool Resize(DirectX12Renderer& renderer, UINT width, UINT height, DXGI_FORMAT format = Format);
        /// <summary>未初期化の描画先を作成します。GPUが参照する既存領域は置換しません。</summary>
        bool Initialize(ID3D12Device* device, UINT width, UINT height, DXGI_FORMAT format = Format);
        /// <summary>
        /// 描画先を設定して色と深度をクリアします。同じコマンドリストで End と対にして呼び出してください。
        /// </summary>
        bool Begin(ID3D12GraphicsCommandList* commands, const std::array<float, 4>& clearColor);
        /// <summary>Begin と同じコマンドリストで描画を終え、色をシェーダーから読み取り可能にします。次の描画先は呼び出し側で設定してください。</summary>
        bool End(ID3D12GraphicsCommandList* commands);
        UINT GetWidth() const noexcept;
        UINT GetHeight() const noexcept;
        /// <summary>描画先の色形式を取得します。未初期化の場合はUNKNOWNを返します。</summary>
        DXGI_FORMAT GetFormat() const noexcept;
        ID3D12Resource* GetResource() const noexcept;
        /// <summary>
        /// UI のディスクリプターヒープへコピーするための CPU SRV です。このヒープ自体を ImGui にバインドしないでください。
        /// </summary>
        D3D12_CPU_DESCRIPTOR_HANDLE GetShaderResourceView() const noexcept;

    private:
        struct Resources;
        static bool CreateColor(ID3D12Device* device, Resources& resources);
        static bool CreateViews(ID3D12Device* device, Resources& resources);
        void Transition(ID3D12GraphicsCommandList* commands, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after);
        std::unique_ptr<Resources> resources_;
        ID3D12GraphicsCommandList* recording_ = nullptr;
    };
}
