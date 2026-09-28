#pragma once

#include <Engine/Platform/Window.h>
#include <Engine/Graphics/Texture2D.h>
#include <d3d12.h>
#include <wrl/client.h>

#include <array>
#include <filesystem>

namespace Engine
{
    struct TriangleVertex
    {
        std::array<float, 3> position;
        std::array<float, 4> color;
        std::array<float, 2> uv;
    };

    class TriangleRenderer final
    {
    public:
        /// <summary>
        /// 三角形の描画リソースを管理するオブジェクトを初期化します。
        /// </summary>
        TriangleRenderer() = default;

        /// <summary>
        /// 描画リソースを解放します。破棄前に利用中の GPU 処理を完了させてください。
        /// </summary>
        ~TriangleRenderer() = default;

        /// <summary>
        /// 描画リソースの二重所有を防ぐため、コピー生成を禁止します。
        /// </summary>
        TriangleRenderer(const TriangleRenderer&) = delete;

        /// <summary>
        /// 描画リソースの二重所有を防ぐため、コピー代入を禁止します。
        /// </summary>
        TriangleRenderer& operator=(const TriangleRenderer&) = delete;

        /// <summary>
        /// ルートシグネチャ、パイプライン、頂点バッファー、テクスチャを生成します。
        /// </summary>
        /// <param name="device">生成に使用する DirectX 12 デバイス。</param>
        /// <param name="queue">テクスチャの初期転送に使用する DIRECT 型の描画キュー。</param>
        /// <param name="texturePath">貼り付ける画像ファイル。</param>
        /// <param name="shaderPath">VSMain と PSMain を定義した HLSL ファイル。</param>
        /// <param name="vertices">描画する三角形の頂点位置、色、UV 座標。</param>
        /// <returns>初期化に成功した場合は true、失敗または初期化済みの場合は false。</returns>
        bool Initialize(ID3D12Device* device, ID3D12CommandQueue* queue, const std::filesystem::path& texturePath,
            const std::filesystem::path& shaderPath,
            const std::array<TriangleVertex, 3>& vertices);

        /// <summary>
        /// 記録中のコマンドリストに三角形の描画命令を追加します。
        /// </summary>
        /// <param name="commands">レンダーターゲットとビューポートが設定済みのコマンドリスト。</param>
        /// <param name="aspectRatio">描画領域の幅÷高さ。三角形の縦横比を維持するために使用します。</param>
        /// <param name="rotationY">原点を通る Y 軸周りの回転角度（ラジアン）。</param>
        void Draw(ID3D12GraphicsCommandList* commands, float aspectRatio, float rotationY = 0.0f) const;

    private:
        /// <summary>
        /// 縦横比補正・Y 軸回転・テクスチャ SRV とサンプラーを定義するルートシグネチャを生成します。
        /// </summary>
        /// <param name="device">生成に使用するデバイス。</param>
        /// <returns>生成に成功した場合は true。</returns>
        bool CreateRootSignature(ID3D12Device* device);

        /// <summary>
        /// シェーダーをコンパイルし、色付き三角形のパイプラインを生成します。
        /// </summary>
        /// <param name="device">生成に使用するデバイス。</param>
        /// <param name="shaderPath">コンパイルする HLSL ファイル。</param>
        /// <returns>コンパイルと生成に成功した場合は true。</returns>
        bool CreatePipelineState(ID3D12Device* device, const std::filesystem::path& shaderPath);

        /// <summary>
        /// アップロードヒープ上の頂点バッファーを生成し、頂点を一度だけ書き込みます。
        /// </summary>
        /// <param name="device">生成に使用するデバイス。</param>
        /// <param name="vertices">転送する三頂点。</param>
        /// <returns>生成と書き込みに成功した場合は true。</returns>
        bool CreateVertexBuffer(ID3D12Device* device, const std::array<TriangleVertex, 3>& vertices);

        bool initialized_ = false;
        Texture2D texture_;
        Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;
        Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffer_;
        D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};
    };
}
