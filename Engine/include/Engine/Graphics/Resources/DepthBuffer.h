#pragma once

#include <Engine/Platform/Window.h>
#include <d3d12.h>
#include <wrl/client.h>

namespace Engine
{
    class DepthBuffer final
    {
    public:
        static constexpr DXGI_FORMAT Format = DXGI_FORMAT_D32_FLOAT;

        /// <summary>
        /// 深度バッファーのリソース管理を初期化します。
        /// </summary>
        DepthBuffer() = default;

        /// <summary>
        /// 深度リソースを解放します。事前に利用中の GPU 処理を完了させてください。
        /// </summary>
        ~DepthBuffer() = default;

        /// <summary>
        /// 深度リソースの共有を防ぐため、コピー生成を禁止します。
        /// </summary>
        DepthBuffer(const DepthBuffer&) = delete;

        /// <summary>
        /// 深度リソースの共有を防ぐため、コピー代入を禁止します。
        /// </summary>
        DepthBuffer& operator=(const DepthBuffer&) = delete;

        /// <summary>
        /// DEFAULT ヒープに深度バッファーと DSV を生成します。DEPTH_WRITE 状態で使用します。
        /// </summary>
        /// <param name="device">生成に使用するデバイス。</param>
        /// <param name="width">描画領域の幅。</param>
        /// <param name="height">描画領域の高さ。</param>
        /// <returns>生成に成功した場合は true。初期化済みの場合は false。</returns>
        bool Initialize(ID3D12Device* device, UINT width, UINT height);

        /// <summary>
        /// 深度バッファーと DSV ヒープを解放します。GPU 待機後に呼んでください。
        /// </summary>
        void Release();

        /// <summary>
        /// 深度のクリアと描画先設定に使用する DSV を取得します。
        /// </summary>
        /// <returns>DSV の CPU ハンドル。未初期化の場合は空のハンドル。</returns>
        D3D12_CPU_DESCRIPTOR_HANDLE GetHandle() const noexcept;

    private:
        /// <summary>
        /// 深度リソースと最適化クリア値を生成します。
        /// </summary>
        /// <param name="device">生成に使用するデバイス。</param>
        /// <param name="width">深度バッファーの幅。</param>
        /// <param name="height">深度バッファーの高さ。</param>
        /// <returns>生成に成功した場合は true。</returns>
        bool CreateResource(ID3D12Device* device, UINT width, UINT height);

        /// <summary>
        /// DSV ヒープを生成し、深度リソースのビューを登録します。
        /// </summary>
        /// <param name="device">生成に使用するデバイス。</param>
        /// <returns>生成に成功した場合は true。</returns>
        bool CreateView(ID3D12Device* device);

        Microsoft::WRL::ComPtr<ID3D12Resource> resource_;
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap_;
    };
}
