#pragma once

#include <Engine/Platform/Window.h>
#include <Engine/Graphics/DepthBuffer.h>
#if defined(_DEBUG) || defined(ENGINE_DEVELOPMENT)
#include <Engine/Graphics/ImGuiLayer.h>
#endif

#include <array>
#include <functional>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

namespace Engine
{
    enum class RenderResult
    {
        Presented,
        Paused,
        Failed
    };

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
        /// 背景をクリアし、指定された描画処理を記録して表示します。サイズ変更を反映し、最小化中は描画を休止します。
        /// 初期化したスレッドから呼び出してください。
        /// </summary>
        /// <param name="clearColor">赤、緑、青、不透明度の順で指定する 0.0 から 1.0 の色。</param>
        /// <param name="draw">クリア後に呼ぶ描画処理。記録中のコマンドリストと幅÷高さを渡します。ポインターを保持しないでください。</param>
        /// <param name="debugUi">Debug 構成で UI フレーム開始後に呼ぶパネル構築処理。Release では使用しません。</param>
        /// <returns>表示、描画休止、失敗の状態。休止時の待機は呼び出し側で行ってください。</returns>
        RenderResult Render(const std::array<float, 4>& clearColor,
            const std::function<void(ID3D12GraphicsCommandList*, float)>& draw = {},
            const std::function<void()>& debugUi = {});

        /// <summary>
        /// 描画リソースの生成に使用するデバイスを取得します。所有権は移譲しません。
        /// </summary>
        /// <returns>初期化済みのデバイス。未初期化の場合は nullptr。</returns>
        ID3D12Device* GetDevice() const noexcept;

        /// <summary>
        /// 初期リソース転送に使用する描画キューを取得します。所有権は移譲しません。
        /// </summary>
        /// <returns>初期化済みの DIRECT 型キュー。未初期化の場合は nullptr。</returns>
        ID3D12CommandQueue* GetCommandQueue() const noexcept;

        /// <summary>
        /// 現在の描画領域の幅をピクセル単位で取得します。リサイズは Render 内で反映されます。
        /// </summary>
        /// <returns>描画領域の幅。</returns>
        UINT GetWidth() const noexcept;

        /// <summary>
        /// 現在の描画領域の高さをピクセル単位で取得します。リサイズは Render 内で反映されます。
        /// </summary>
        /// <returns>描画領域の高さ。</returns>
        UINT GetHeight() const noexcept;

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
        /// 指定したフェンス値まで GPU の処理が完了するのを待機します。
        /// </summary>
        /// <param name="target">再利用するフレームなどに対応する完了値。</param>
        /// <returns>待機に成功し、デバイスが有効な場合は true、失敗した場合は false。</returns>
        bool WaitForFence(UINT64 target);

        /// <summary>
        /// 描画先を検証し、初期サイズを取得します。
        /// </summary>
        /// <param name="handle">描画先のウィンドウ。</param>
        /// <returns>描画先とサイズが有効な場合は true、無効な場合は false。</returns>
        bool SetTargetWindow(HWND handle);

        /// <summary>
        /// Debug 構成で利用可能な場合にデバッグレイヤーを有効化します。
        /// 利用できない場合は警告を記録し、デバッグレイヤーなしで起動を続けます。
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
        /// フレームごとのアロケーターと描画リストを生成し、リストを閉じた状態にします。
        /// </summary>
        /// <returns>生成とリストのクローズに成功した場合は true、失敗した場合は false。</returns>
        bool CreateDrawingCommands();

        /// <summary>
        /// GPU の完了を待つフェンスとイベントを生成します。
        /// </summary>
        /// <returns>生成に成功した場合は true、失敗した場合は false。</returns>
        bool CreateSynchronizationObjects();

        /// <summary>
        /// フレームごとに描画領域と同じサイズの深度バッファーを生成します。
        /// </summary>
        /// <returns>すべての生成に成功した場合は true。</returns>
        bool CreateDepthBuffers();

        static constexpr UINT bufferCount = 2;
        static constexpr DXGI_FORMAT bufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

#if defined(_DEBUG) || defined(ENGINE_DEVELOPMENT)
        ImGuiLayer debugUi_;
#endif
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
        std::array<Microsoft::WRL::ComPtr<ID3D12CommandAllocator>, bufferCount> allocators;
        std::array<UINT64, bufferCount> frameFenceValues{};
        std::array<DepthBuffer, bufferCount> depthBuffers;
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commands;
        Microsoft::WRL::ComPtr<ID3D12Fence> fence;
    };
}
