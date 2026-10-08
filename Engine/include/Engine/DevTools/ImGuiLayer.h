#pragma once

#if defined(_DEBUG) || defined(ENGINE_DEVELOPMENT)
#include <Engine/Platform/Window.h>
#include <d3d12.h>
#include <wrl/client.h>

#include <array>

struct ImGuiContext;

namespace Engine
{
    class ImGuiLayer final
    {
    public:
        /// <summary>
        /// Debug 用 UI のリソース管理を初期化します。
        /// </summary>
        ImGuiLayer() = default;

        /// <summary>
        /// UI のリソースを解放します。事前に利用中の GPU 処理を完了させてください。
        /// </summary>
        ~ImGuiLayer();

        /// <summary>
        /// コンテキストの二重所有を防ぐため、コピー生成を禁止します。
        /// </summary>
        ImGuiLayer(const ImGuiLayer&) = delete;

        /// <summary>
        /// コンテキストの二重所有を防ぐため、コピー代入を禁止します。
        /// </summary>
        ImGuiLayer& operator=(const ImGuiLayer&) = delete;

        /// <summary>
        /// Win32・DirectX 12 バックエンドと UI 用のディスクリプターヒープを生成します。
        /// </summary>
        /// <param name="window">入力と描画の対象ウィンドウ。</param>
        /// <param name="device">リソースを生成するデバイス。</param>
        /// <param name="queue">テクスチャ転送に使用する描画キュー。</param>
        /// <param name="framesInFlight">同時に GPU で処理するフレーム数。</param>
        /// <param name="format">レンダーターゲットの形式。</param>
        /// <returns>すべての初期化に成功した場合は true。</returns>
        bool Initialize(HWND window, ID3D12Device* device, ID3D12CommandQueue* queue,
            int framesInFlight, DXGI_FORMAT format);

        /// <summary>
        /// バックエンド、コンテキスト、ヒープを解放します。GPU 待機後に呼んでください。
        /// </summary>
        void Shutdown();

        /// <summary>
        /// UI の入力を更新し、新しいフレームの構築を開始します。
        /// </summary>
        void BeginFrame();

        /// <summary>
        /// UI フレームを確定し、シーン描画後のコマンドリストへ描画命令を追加します。
        /// </summary>
        /// <param name="commands">レンダーターゲットが設定済みのコマンドリスト。</param>
        void Render(ID3D12GraphicsCommandList* commands);

        /// <summary>
        /// 初期化済みの場合に Win32 のメッセージを UI へ渡します。
        /// </summary>
        /// <param name="window">メッセージを受け取ったウィンドウ。</param>
        /// <param name="message">メッセージの種類。</param>
        /// <param name="wParam">メッセージの追加情報。</param>
        /// <param name="lParam">メッセージの追加情報。</param>
        /// <returns>バックエンドが処理した場合は true。</returns>
        static bool ProcessMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

        // Copies a Scene/Game/asset SRV into a persistent UI slot. Wait for GPU idle before replacing it.
        D3D12_GPU_DESCRIPTOR_HANDLE SetSceneTexture(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE source, unsigned int slot = 0);

    private:
        /// <summary>
        /// UI テクスチャ用の空きディスクリプターを割り当てます。枯渇時は例外を送出します。
        /// </summary>
        /// <param name="cpu">CPU 側のハンドルの出力先。</param>
        /// <param name="gpu">GPU 側のハンドルの出力先。</param>
        void AllocateDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE& cpu, D3D12_GPU_DESCRIPTOR_HANDLE& gpu);

        /// <summary>
        /// バックエンドが使用を終えたディスクリプターを再利用可能にします。
        /// </summary>
        /// <param name="cpu">解放するディスクリプターの CPU ハンドル。</param>
        void FreeDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE cpu);

        static constexpr UINT DescriptorCount = 64;
        ImGuiContext* context_ = nullptr;
        bool platformInitialized_ = false;
        bool rendererInitialized_ = false;
        D3D12_CPU_DESCRIPTOR_HANDLE sceneCpu_[5]{};
        D3D12_GPU_DESCRIPTOR_HANDLE sceneGpu_[5]{};
        UINT descriptorSize_ = 0;
        std::array<bool, DescriptorCount> allocated_{};
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> descriptorHeap_;
    };
}
#endif
