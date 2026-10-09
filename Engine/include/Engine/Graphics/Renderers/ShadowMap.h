#pragma once
#include <Engine/Graphics/Resources/RenderTargetBinding.h>
#include <Engine/Graphics/Camera.h>
#include <Engine/Graphics/Materials/DirectionalLight.h>
#include <wrl/client.h>
#include <filesystem>

namespace Engine
{
    class ShadowMap final
    {
    public:
        static constexpr UINT Resolution=2048;
        static constexpr UINT LocalResolution=512,LocalFaces=32;
        /// <summary>光源視点の深度テクスチャと深度専用パイプラインを生成します。</summary>
        bool Initialize(ID3D12Device* device,const std::filesystem::path& shader,bool localAtlas=false);
        /// <summary>カメラ近傍を覆う光源視点を計算し、深度描画を開始します。</summary>
        bool Begin(ID3D12GraphicsCommandList* commands,const Camera& camera,const DirectionalLight& light);
        /// <summary>点光源の立方体面またはスポット円錐の深度描画を開始します。</summary>
        bool BeginLocal(ID3D12GraphicsCommandList* commands,const LocalLight& light,UINT slice,UINT face=0);
        /// <summary>局所光源アトラスの各投影行列を取得します。</summary>
        const std::array<DirectX::XMFLOAT4X4,LocalFaces>& LocalMatrices() const { return localMatrices_; }
        /// <summary>深度を参照可能に遷移させ、直前の出力先を復元します。</summary>
        void End(ID3D12GraphicsCommandList* commands);
        /// <summary>入力済みの頂点・インデックスを深度専用で描画します。</summary>
        void Draw(ID3D12GraphicsCommandList* commands,const DirectX::XMFLOAT4X4& world,
            const D3D12_VERTEX_BUFFER_VIEW& vertices,const D3D12_INDEX_BUFFER_VIEW& indices,UINT count,D3D12_GPU_DESCRIPTOR_HANDLE palette={}) const;
        /// <summary>影の参照用CPUディスクリプターを取得します。</summary>
        D3D12_CPU_DESCRIPTOR_HANDLE View() const { return srv_->GetCPUDescriptorHandleForHeapStart(); }
        /// <summary>参照する深度リソースを取得します。</summary>
        ID3D12Resource* Resource() const { return depth_.Get(); }
        /// <summary>画素シェーダーと共通の光源投影パラメーターを返します。</summary>
        const std::array<float,6>& Constants() const { return constants_; }
    private:
        /// <summary>深度用リソースとDSV・SRVを生成します。</summary>
        bool CreateDepth(ID3D12Device* device);
        /// <summary>位置だけを入力する光源深度パイプラインを生成します。</summary>
        bool CreatePipeline(ID3D12Device* device,const std::filesystem::path& shader);
        /// <summary>深度書き込みとシェーダー参照の状態を同期します。</summary>
        void Transition(ID3D12GraphicsCommandList* commands,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after);
        Microsoft::WRL::ComPtr<ID3D12Resource> depth_;
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsv_,srv_;
        Microsoft::WRL::ComPtr<ID3D12RootSignature> root_;
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> emptyPalette_;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline_;
        DirectX::XMFLOAT4X4 viewProjection_{};
        std::array<float,6> constants_{};
        RenderTargetBinding previous_;
        UINT resolution_=Resolution,layers_=1,descriptorStride_=0;
        std::array<DirectX::XMFLOAT4X4,LocalFaces> localMatrices_{};
    };
}
