#pragma once

#include <Engine/Graphics/Renderers/MeshRenderer.h>
#include <Engine/Animation/Skeleton.h>
#include <Engine/Graphics/Materials/UvTransform.h>
#include <Engine/Graphics/Materials/Material.h>
#include <Engine/Graphics/Materials/DirectionalLight.h>
#include <DirectXMath.h>
#include <DirectXCollision.h>
#include <array>
#include <cstdint>
#include <vector>
#include <memory>
#include <optional>

namespace Engine
{
    class ModelRenderer final
    {
    public:
        class PreparedPose final
        {
        public:
            /// <summary>無効な姿勢候補を生成します。</summary>
            PreparedPose() = default;
            /// <summary>姿勢候補の所有権を移動します。</summary>
            PreparedPose(PreparedPose&&) noexcept = default;
            /// <summary>姿勢候補の所有権を移動して代入します。</summary>
            PreparedPose& operator=(PreparedPose&&) noexcept = default;
            /// <summary>候補の複製を禁止します。</summary>
            PreparedPose(const PreparedPose&) = delete;
            /// <summary>候補のコピー代入を禁止します。</summary>
            PreparedPose& operator=(const PreparedPose&) = delete;
        private:
            friend class ModelRenderer;
            const ModelRenderer* owner_=nullptr;
            std::shared_ptr<const SkeletonData> rig_;
            std::vector<BonePose> pose_;
            std::vector<DirectX::XMFLOAT4X4> matrices_;
            std::vector<std::vector<SkinMatrix>> palettes_;
            DirectX::BoundingBox bounds_{};
        };
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

        size_t MeshCount() const { return meshes_.size(); }
        size_t TriangleCount() const { return triangleCount_; }
        /// <summary>指定メッシュの共有頂点領域を取得します。</summary>
        ID3D12Resource* GeometryResource(size_t index) const { return meshes_.at(index)->GeometryResource(); }
        const DirectX::BoundingBox& Bounds() const { return bounds_; }
        const std::shared_ptr<const SkeletonData>& Rig() const { return rig_; }
        const std::vector<BonePose>& Pose() const { return pose_; }
        std::shared_ptr<ModelRenderer> AnimatedCopy(ID3D12Device* device,ID3D12CommandQueue* queue) const;
        /// <summary>姿勢を検証して反映します。頂点バッファーは変更しません。</summary>
        bool ApplyPose(const std::vector<BonePose>& pose);
        /// <summary>行列とBoundsを計算して候補に保存します。失敗時は現在のモデルと出力候補を保持します。</summary>
        bool PreparePose(const std::vector<BonePose>& pose,PreparedPose& result) const;
        /// <summary>このモデルで準備した候補を割り当てなしで反映します。候補は1度だけ使用できます。</summary>
        bool ApplyPreparedPose(PreparedPose&& pose) noexcept;
        /// <summary>モデル内のすべてのメッシュを光源の深度へ描画します。</summary>
        void DrawShadow(ID3D12GraphicsCommandList* commands,const DirectX::XMFLOAT4X4& world,const ShadowMap& shadow) const;
        // ローカル空間の単位レイを三角形へ当て、最も近い交点距離を返します。
        bool IntersectRay(DirectX::FXMVECTOR origin, DirectX::FXMVECTOR direction, float& distance) const;
    private:
        bool Rebuild(const std::vector<MeshData>& data);
        /// <summary>ボーンごとの頂点領域を一度だけ集計します。</summary>
        void PrepareSkinBounds(const std::vector<MeshData>& data);
        /// <summary>選択判定のため、現在の姿勢の三角形を必要時に生成します。</summary>
        void PreparePickGeometry() const;
        std::shared_ptr<const SkeletonData> rig_;
        std::vector<BonePose> pose_;
        std::shared_ptr<MeshResources> resources_;
        Microsoft::WRL::ComPtr<ID3D12Device> device_;
        Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue_;
        DirectX::BoundingBox bounds_{};
        size_t triangleCount_=0;
        mutable bool pickDirty_=false;
        mutable std::vector<DirectX::XMFLOAT3> trianglePositions_;
        std::vector<std::vector<std::optional<DirectX::BoundingBox>>> skinBounds_;
        std::vector<std::vector<SkinMatrix>> palettes_;
        std::vector<DirectX::XMFLOAT4X4> nodeMatrices_;
        std::vector<std::shared_ptr<MeshRenderer>> meshes_;
    };
}
