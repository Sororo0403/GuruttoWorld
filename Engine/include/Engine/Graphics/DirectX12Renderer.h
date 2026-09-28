#pragma once

#include <Engine/Platform/Window.h>

#include <array>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

namespace Engine
{
    class DirectX12Renderer final
    {
    public:
        /// <summary>
        /// DirectX 12 の描画リソースを管理するオブジェクトを生成します。
        /// </summary>
        DirectX12Renderer();

        /// <summary>
        /// GPU の処理完了を待機し、描画リソースを解放します。
        /// </summary>
        ~DirectX12Renderer();

        /// <summary>
        /// 描画リソースの二重所有を防ぐため、コピー生成を禁止します。
        /// </summary>
        DirectX12Renderer(const DirectX12Renderer&) = delete;

        /// <summary>
        /// 描画リソースの二重所有を防ぐため、コピー代入を禁止します。
        /// </summary>
        DirectX12Renderer& operator=(const DirectX12Renderer&) = delete;

        /// <summary>
        /// ウィンドウに対応するデバイス、スワップチェーン、描画コマンドを初期化します。
        /// </summary>
        /// <param name="handle">描画先のウィンドウ。描画クラスより後に破棄してください。</param>
        /// <returns>初期化に成功した場合は true、失敗または初期化済みの場合は false。</returns>
        bool Initialize(HWND handle);

        /// <summary>
        /// 背景を指定色でクリアして表示します。サイズ変更を反映し、最小化中は描画を休止します。
        /// 初期化したスレッドから呼び出してください。
        /// </summary>
        /// <param name="clearColor">赤、緑、青、不透明度の順で指定する 0.0 から 1.0 の色。</param>
        /// <returns>描画または休止に成功した場合は true、失敗した場合は false。</returns>
        bool Render(const std::array<float, 4>& clearColor);

    private:
        /// <summary>
        /// GPU の処理完了を待ち、生成途中のリソースも含めて解放します。
        /// </summary>
        void ReleaseResources();

        /// <summary>
        /// GPU に送信したコマンドの完了を待機します。
        /// </summary>
        /// <returns>待機に成功し、デバイスが有効な場合は true、失敗した場合は false。</returns>
        bool WaitForGpu();

        /// <summary>
        /// 描画先を検証し、初期サイズを取得します。
        /// </summary>
        /// <param name="handle">描画先のウィンドウ。</param>
        /// <returns>描画先とサイズが有効な場合は true、無効な場合は false。</returns>
        bool SetTargetWindow(HWND handle);

        /// <summary>
        /// Debug 構成で利用可能な場合にデバッグレイヤーを有効化します。
        /// </summary>
        void EnableDebugLayer();

        /// <summary>
        /// アダプター列挙とスワップチェーン生成に使用する DXGI ファクトリーを生成します。
        /// </summary>
        /// <returns>生成に成功した場合は true、失敗した場合は false。</returns>
        bool CreateFactory();

        /// <summary>
        /// 対応するハードウェアデバイスを生成し、見つからない場合は WARP を使用します。
        /// </summary>
        /// <returns>生成に成功した場合は true、失敗した場合は false。</returns>
        bool CreateDevice();

        /// <summary>
        /// 描画コマンドを GPU に送信するキューを生成します。
        /// </summary>
        /// <returns>生成に成功した場合は true、失敗した場合は false。</returns>
        bool CreateCommandQueue();

        /// <summary>
        /// ウィンドウ用のダブルバッファースワップチェーンを生成します。
        /// </summary>
        /// <returns>生成に成功した場合は true、失敗した場合は false。</returns>
        bool CreateSwapChain();

        /// <summary>
        /// レンダーターゲットのディスクリプターヒープを生成します。
        /// </summary>
        /// <returns>生成に成功した場合は true、失敗した場合は false。</returns>
        bool CreateRenderTargetHeap();

        /// <summary>
        /// スワップチェーンの各バッファーを取得し、レンダーターゲットビューを生成します。
        /// </summary>
        /// <returns>生成に成功した場合は true、失敗した場合は false。</returns>
        bool CreateRenderTargets();

        /// <summary>
        /// 描画コマンドのアロケーターとリストを生成し、リストを閉じた状態にします。
        /// </summary>
        /// <returns>生成とリストのクローズに成功した場合は true、失敗した場合は false。</returns>
        bool CreateDrawingCommands();

        /// <summary>
        /// GPU の完了を待つフェンスとイベントを生成します。
        /// </summary>
        /// <returns>生成に成功した場合は true、失敗した場合は false。</returns>
        bool CreateSynchronizationObjects();

        static constexpr UINT bufferCount = 2;
        static constexpr DXGI_FORMAT bufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

        HWND window = nullptr;
        UINT width = 0;
        UINT height = 0;
        UINT descriptorSize = 0;
        UINT64 fenceValue = 0;
        HANDLE fenceEvent = nullptr;
        bool ready = false;
        bool occluded = false;
        Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
        Microsoft::WRL::ComPtr<ID3D12Device> device;
        Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue;
        Microsoft::WRL::ComPtr<IDXGISwapChain3> swapChain;
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> renderTargetHeap;
        std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, bufferCount> buffers;
        Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator;
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commands;
        Microsoft::WRL::ComPtr<ID3D12Fence> fence;
    };
}
