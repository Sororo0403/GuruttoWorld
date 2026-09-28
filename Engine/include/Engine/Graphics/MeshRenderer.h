#pragma once

#include <Engine/Graphics/Texture2D.h>
#include <Engine/Graphics/UVTransform.h>
#include <Engine/Graphics/DirectionalLight.h>
#include <DirectXMath.h>
#include <array>
#include <cstdint>
#include <span>
#include <Engine/Graphics/MeshData.h>

namespace Engine
{
    class MeshRenderer final
    {
    public:
        /// <summary>
        /// メッシュの描画リソース管理を初期化します。
        /// </summary>
        MeshRenderer() = default;

        /// <summary>
        /// 描画リソースを解放します。事前に利用中の GPU 処理を完了させてください。
        /// </summary>
        ~MeshRenderer() = default;

        /// <summary>
        /// リソースの二重所有を防ぐため、コピー生成を禁止します。
        /// </summary>
        MeshRenderer(const MeshRenderer&) = delete;

        /// <summary>
        /// リソースの二重所有を防ぐため、コピー代入を禁止します。
        /// </summary>
        MeshRenderer& operator=(const MeshRenderer&) = delete;

        /// <summary>
        /// メッシュ・テクスチャ・パイプラインを生成し、GPU 転送の完了を待機します。
        /// </summary>
        /// <param name="device">生成に使用するデバイス。</param>
        /// <param name="queue">初期転送に使用する DIRECT 型キュー。</param>
        /// <param name="mesh">頂点・インデックス・テクスチャを持つメッシュ。</param>
        /// <param name="shaderPath">メッシュ用 HLSL ファイル。</param>
        /// <returns>生成に成功した場合は true。初期化済みの場合は false。</returns>
        bool Initialize(ID3D12Device* device, ID3D12CommandQueue* queue,
            const MeshData& mesh, const std::filesystem::path& shaderPath);

        /// <summary>
        /// 深度テスト・裏面除去・陰影を使ってメッシュを描画します。
        /// </summary>
        /// <param name="commands">描画先・D32_FLOAT 深度・ビューポートが設定済みのコマンドリスト。</param>
        /// <param name="world">メッシュのワールド行列。逆行列を持つアフィン変換を指定してください。</param>
        /// <param name="viewProjection">ビュー行列と射影行列をこの順に乗算した行列。</param>
        /// <param name="light">平行光源、環境光、ハイライトの設定。</param>
        /// <param name="cameraPosition">ビュー行列と対応するワールド空間のカメラ位置。</param>
        /// <param name="uvTransform">テクスチャ座標の拡縮・回転・移動。</param>
        void Draw(ID3D12GraphicsCommandList* commands, const DirectX::XMFLOAT4X4& world,
            const DirectX::XMFLOAT4X4& viewProjection, const DirectionalLight& light = {},
            const std::array<float, 3>& cameraPosition = { 0.0f, 0.0f, -3.5f },
            const UVTransform& uvTransform = {}) const;

    private:
        /// <summary>
        /// 変換行列・テクスチャ・サンプラーを渡すルートシグネチャを生成します。
        /// </summary>
        /// <param name="device">生成に使用するデバイス。</param>
        /// <returns>生成に成功した場合は true。</returns>
        bool CreateRootSignature(ID3D12Device* device);

        /// <summary>
        /// メッシュ用のシェーダーと深度・裏面除去を有効にしたパイプラインを生成します。
        /// </summary>
        /// <param name="device">生成に使用するデバイス。</param>
        /// <param name="shaderPath">コンパイルする HLSL ファイル。</param>
        /// <returns>生成に成功した場合は true。</returns>
        bool CreatePipelineState(ID3D12Device* device, const std::filesystem::path& shaderPath);

        bool initialized_ = false;
        UINT indexCount_ = 0;
        Texture2D texture_;
        Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;
        Microsoft::WRL::ComPtr<ID3D12Resource> meshBuffer_;
        D3D12_VERTEX_BUFFER_VIEW vertexView_{};
        D3D12_INDEX_BUFFER_VIEW indexView_{};
    };
}
