#pragma once

#include <Engine/Graphics/Renderers/MeshRenderer.h>
#include <Engine/Graphics/Materials/UvTransform.h>
#include <Engine/Graphics/Materials/Material.h>
#include <Engine/Graphics/Materials/DirectionalLight.h>
#include <DirectXMath.h>
#include <DirectXCollision.h>
#include <array>
#include <cstdint>
#include <vector>
#include <memory>

namespace Engine
{
    class ModelRenderer final
    {
    public:
        /// <summary>
        /// モデルの描画リソース管理を初期化します。
        /// </summary>
        ModelRenderer() = default;

        /// <summary>
        /// 描画リソースを解放します。事前に利用中の GPU 処理を完了させてください。
        /// </summary>
        ~ModelRenderer() = default;

        /// <summary>
        /// リソースの二重所有を防ぐため、コピー生成を禁止します。
        /// </summary>
        ModelRenderer(const ModelRenderer&) = delete;

        /// <summary>
        /// リソースの二重所有を防ぐため、コピー代入を禁止します。
        /// </summary>
        ModelRenderer& operator=(const ModelRenderer&) = delete;

        /// <summary>
        /// OBJ の各メッシュ・テクスチャ・パイプラインを生成し、GPU 転送の完了を待機します。
        /// </summary>
        /// <param name="device">生成に使用するデバイス。</param>
        /// <param name="queue">初期転送に使用する DIRECT 型キュー。</param>
        /// <param name="modelPath">読み込む OBJ ファイル。</param>
        /// <param name="shaderPath">モデル用 HLSL ファイル。</param>
        /// <returns>生成に成功した場合は true。初期化済みの場合は false。</returns>
        bool Initialize(ID3D12Device* device, ID3D12CommandQueue* queue,
            const std::filesystem::path& modelPath, const std::filesystem::path& shaderPath);

        /// <summary>
        /// 深度テスト・裏面除去・陰影を使ってモデルを描画します。
        /// </summary>
        /// <param name="commands">描画先・D32_FLOAT 深度・ビューポートが設定済みのコマンドリスト。</param>
        /// <param name="world">モデルのワールド行列。逆行列を持つアフィン変換を指定してください。</param>
        /// <param name="viewProjection">ビュー行列と射影行列をこの順に乗算した行列。</param>
        /// <param name="light">平行光源、環境光、ハイライトの設定。</param>
        /// <param name="cameraPosition">ビュー行列と対応するワールド空間のカメラ位置。</param>
        /// <param name="uvTransform">テクスチャ座標の拡縮・回転・移動。</param>
        void Draw(ID3D12GraphicsCommandList* commands, const DirectX::XMFLOAT4X4& world,
            const DirectX::XMFLOAT4X4& viewProjection, const DirectionalLight& light = {},
            const std::array<float, 3>& cameraPosition = { 0.0f, 0.0f, -3.5f },
            const UvTransform& uvTransform = {},const Material* material=nullptr) const;

        const DirectX::BoundingBox& Bounds() const { return bounds_; }
        /// <summary>モデル内のすべてのメッシュを光源の深度へ描画します。</summary>
        void DrawShadow(ID3D12GraphicsCommandList* commands,const DirectX::XMFLOAT4X4& world,const ShadowMap& shadow) const;
        // ローカル空間の単位レイを三角形へ当て、最も近い交点距離を返します。
        bool IntersectRay(DirectX::FXMVECTOR origin, DirectX::FXMVECTOR direction, float& distance) const;
    private:
        DirectX::BoundingBox bounds_{};
        std::vector<DirectX::XMFLOAT3> trianglePositions_;
        std::vector<std::unique_ptr<MeshRenderer>> meshes_;
    };
}
