#pragma once

#include <Engine/Graphics/Renderers/MeshResources.h>
#include <Engine/Graphics/Materials/UvTransform.h>
#include <Engine/Graphics/Materials/Material.h>
#include <Engine/Graphics/Materials/DirectionalLight.h>
#include <DirectXMath.h>
#include <array>
#include <cstdint>
#include <span>
#include <tuple>
#include <limits>
#include <Engine/Graphics/Models/MeshData.h>
#include <Engine/Graphics/Renderers/ShadowMap.h>
#include <Engine/Graphics/Resources/ConstantBuffer.h>

namespace Engine
{
    struct MeshDeformation {
        std::span<const SkinMatrix> palette;
        std::span<const DirectX::XMFLOAT4X4> instances;
    };
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
        /// 既存のパイプラインとテクスチャキャッシュを共有し、固有のメッシュだけを生成します。
        /// </summary>
        /// <param name="device">共有リソースと同じデバイス。</param>
        /// <param name="queue">転送に使用する DIRECT 型キュー。</param>
        /// <param name="mesh">描画するメッシュ。</param>
        /// <param name="resources">初期化済みの共有リソース。</param>
        /// <returns>初期化に成功した場合は true。</returns>
        bool Initialize(ID3D12Device* device, ID3D12CommandQueue* queue,
            const MeshData& mesh, const std::shared_ptr<MeshResources>& resources);

        /// <summary>
        /// 深度テスト・裏面除去・陰影を使ってメッシュを描画します。
        /// </summary>
        /// <param name="commands">フェンス待機済みの RenderFrameContext と描画先・深度・ビューポートが設定済みのコマンドリスト。</param>
        /// <param name="world">メッシュのワールド行列。逆行列を持つアフィン変換を指定してください。</param>
        /// <param name="viewProjection">ビュー行列と射影行列をこの順に乗算した行列。</param>
        /// <param name="light">平行光源、環境光、ハイライトの設定。</param>
        /// <param name="cameraPosition">ビュー行列と対応するワールド空間のカメラ位置。</param>
        /// <param name="uvTransform">テクスチャ座標の拡縮・回転・移動。</param>
        void Draw(ID3D12GraphicsCommandList* commands, const DirectX::XMFLOAT4X4& world,
            const DirectX::XMFLOAT4X4& viewProjection, const DirectionalLight& light = {},
            const std::array<float, 3>& cameraPosition = { 0.0f, 0.0f, -3.5f },
            const UvTransform& uvTransform = {},const Material* material=nullptr,const MeshDeformation& deformation={}) const;

        /// <summary>同じメッシュの実形状を光源視点の深度へ描画します。</summary>
        void DrawShadow(ID3D12GraphicsCommandList* commands,const DirectX::XMFLOAT4X4& world,const ShadowMap& shadow,std::span<const SkinMatrix> palette={}) const;
        /// <summary>不変の頂点領域を取得し、姿勢変更時に再作成しないことを確認します。</summary>
        ID3D12Resource* GeometryResource() const noexcept { return meshBuffer_.Get(); }
        size_t TriangleCount() const noexcept {return indexCount_/3;}
    private:
        struct TransformConstants
        {
            DirectX::XMFLOAT4X4 worldViewProjection;
            DirectX::XMFLOAT4 worldRows[3];
            std::array<float,4> color{1,1,1,1};
        };
        struct DrawBindings {
            bool mirrored,hdr;
            const Material* material;
            const TransformConstants* transform;
            const std::array<float,26>* lighting;
            const UvTransform* uv;
            ID3D12DescriptorHeap* heap;
        };
        static TransformConstants MakeTransforms(DirectX::FXMMATRIX worldMatrix,const DirectX::XMFLOAT4X4& viewProjection,const Material* material);
        ID3D12DescriptorHeap* SurfaceBindings(ID3D12GraphicsCommandList* commands,const DirectionalLight& light,const Material* material,const LocalLightView& lights,const SkinPaletteView& skin,const SkinPaletteView& instances) const;
        /// <summary>描画単位の汎用パラメーターと GPU リソースを設定します。</summary>
        bool BindDraw(ID3D12GraphicsCommandList* commands,const DrawBindings& bindings) const;

        bool PrepareLocalLighting(ID3D12GraphicsCommandList* commands,const DirectionalLight& light,const Material* material,std::array<float,26>& constants,LocalLightView& lights) const;
        bool PrepareDeformation(ID3D12GraphicsCommandList* commands,const MeshDeformation& deformation,const DirectX::XMFLOAT4X4& viewProjection,bool mirrored,SkinPaletteView& view) const;
        /// <summary>不変ディスクリプターを共有し、フェンス完了済みのフレーム枠から古い参照を解放します。</summary>
        struct BindingRequest {
            const ShadowMap* shadow=nullptr;
            std::shared_ptr<const Texture2D> texture,normal;
            LocalLightView lights;
            SkinPaletteView palette,instances;
            const ShadowMap* localShadow=nullptr;
            std::array<std::shared_ptr<const Texture2D>,2> surfaceImages{};
        };
        struct Binding;
        ID3D12DescriptorHeap* Bindings(ID3D12GraphicsCommandList* commands,const BindingRequest& request) const;
        std::shared_ptr<Binding> CreateBinding(const BindingRequest& request) const;
        using BindingKey=std::tuple<ID3D12Resource*,const Texture2D*,const Texture2D*,ID3D12Resource*,ID3D12Resource*,ID3D12Resource*,ID3D12Resource*,const Texture2D*,const Texture2D*>;
        struct Binding {
            Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap;
            std::shared_ptr<const Texture2D> texture,normal,environment,lightmap;
            std::array<Microsoft::WRL::ComPtr<ID3D12Resource>,5> resources;
        };
        struct BindingFrame {
            UINT64 serial=std::numeric_limits<UINT64>::max();
            std::map<BindingKey,std::shared_ptr<Binding>> entries;
        };
        mutable std::map<BindingKey,std::weak_ptr<Binding>> bindings_;
        mutable std::array<BindingFrame,RenderFrameContext::SlotCount> bindingFrames_;
        // フレーム情報のない独自コマンドでは、従来どおり呼び出し側がGPU完了を管理します。
        mutable std::map<BindingKey,std::shared_ptr<Binding>> unmanagedBindings_;
        size_t requiredPaletteSize_=1;
        mutable ConstantBuffer lightingBuffer_;
        bool initialized_ = false;
        UINT indexCount_ = 0;
        std::shared_ptr<MeshResources> resources_;
        std::shared_ptr<Texture2D> texture_;
        Microsoft::WRL::ComPtr<ID3D12Resource> meshBuffer_;
        D3D12_VERTEX_BUFFER_VIEW vertexView_{};
        D3D12_INDEX_BUFFER_VIEW indexView_{};
    };
}
