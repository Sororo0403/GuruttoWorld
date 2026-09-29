#pragma once

#include <Engine/Graphics/Renderers/MeshRenderer.h>
#include <Engine/Graphics/Materials/UvTransform.h>
#include <Engine/Graphics/Materials/DirectionalLight.h>
#include <DirectXMath.h>
#include <array>
#include <cstdint>
#include <vector>

namespace Engine
{
    class SphereRenderer final
    {
    public:
        /// <summary>
        /// 球体の描画リソース管理を初期化します。
        /// </summary>
        SphereRenderer() = default;

        /// <summary>
        /// 描画リソースを解放します。事前に利用中の GPU 処理を完了させてください。
        /// </summary>
        ~SphereRenderer() = default;

        /// <summary>
        /// リソースの二重所有を防ぐため、コピー生成を禁止します。
        /// </summary>
        SphereRenderer(const SphereRenderer&) = delete;

        /// <summary>
        /// リソースの二重所有を防ぐため、コピー代入を禁止します。
        /// </summary>
        SphereRenderer& operator=(const SphereRenderer&) = delete;

        /// <summary>
        /// 半径 1 の球体メッシュ・テクスチャ・パイプラインを生成し、GPU 転送の完了を待機します。
        /// </summary>
        /// <param name="device">生成に使用するデバイス。</param>
        /// <param name="queue">初期転送に使用する DIRECT 型キュー。</param>
        /// <param name="texturePath">球体に貼り付ける画像。</param>
        /// <param name="shaderPath">球体用 HLSL ファイル。</param>
        /// <returns>生成に成功した場合は true。初期化済みの場合は false。</returns>
        bool Initialize(ID3D12Device* device, ID3D12CommandQueue* queue,
            const std::filesystem::path& texturePath, const std::filesystem::path& shaderPath);

        /// <summary>
        /// 深度テスト・裏面除去・陰影を使って球体を描画します。
        /// </summary>
        /// <param name="commands">描画先・D32_FLOAT 深度・ビューポートが設定済みのコマンドリスト。</param>
        /// <param name="world">球体のワールド行列。逆行列を持つアフィン変換を指定してください。</param>
        /// <param name="viewProjection">ビュー行列と射影行列をこの順に乗算した行列。</param>
        /// <param name="light">平行光源、環境光、ハイライトの設定。</param>
        /// <param name="cameraPosition">ビュー行列と対応するワールド空間のカメラ位置。</param>
        /// <param name="uvTransform">テクスチャ座標の拡縮・回転・移動。</param>
        void Draw(ID3D12GraphicsCommandList* commands, const DirectX::XMFLOAT4X4& world,
            const DirectX::XMFLOAT4X4& viewProjection, const DirectionalLight& light = {},
            const std::array<float, 3>& cameraPosition = { 0.0f, 0.0f, -3.5f },
            const UvTransform& uvTransform = {}) const;

    private:
        /// <summary>
        /// 緯度・経度から頂点とインデックスを生成します。UV の継ぎ目を分離し、極の縮退面を除外します。
        /// </summary>
        /// <param name="vertices">頂点の出力先。</param>
        /// <param name="indices">インデックスの出力先。</param>
        void GenerateMesh(std::vector<MeshVertex>& vertices, std::vector<std::uint32_t>& indices);

        MeshRenderer renderer_;
    };
}
