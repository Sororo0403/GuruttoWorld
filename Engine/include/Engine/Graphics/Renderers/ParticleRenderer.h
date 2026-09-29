#pragma once

#include <Engine/Graphics/Resources/Texture2D.h>
#include <DirectXMath.h>
#include <array>
#include <memory>

namespace Engine
{
    class ParticleRenderer final
    {
    public:
        /// <summary>
        /// パーティクル描画リソースの管理を初期化します。
        /// </summary>
        ParticleRenderer() = default;

        /// <summary>
        /// 描画リソースを解放します。事前に利用中の GPU 処理を完了させてください。
        /// </summary>
        ~ParticleRenderer() = default;

        /// <summary>
        /// リソースの二重所有を防ぐため、コピー生成を禁止します。
        /// </summary>
        ParticleRenderer(const ParticleRenderer&) = delete;

        /// <summary>
        /// リソースの二重所有を防ぐため、コピー代入を禁止します。
        /// </summary>
        ParticleRenderer& operator=(const ParticleRenderer&) = delete;

        /// <summary>
        /// パイプラインとメッシュを生成し、TextureManager から取得したテクスチャの所有権を共有します。
        /// </summary>
        /// <param name="device">生成に使用するデバイス。</param>
        /// <param name="queue">メッシュの転送に使用する DIRECT 型キュー。</param>
        /// <param name="texture">同一デバイスで読み込み済みの共有テクスチャ。画像の再読み込みは行いません。</param>
        /// <param name="shaderPath">パーティクル用の VSMain・PSMain を定義した HLSL ファイル。</param>
        /// <returns>生成に成功した場合は true。初期化済みの場合は false。</returns>
        bool Initialize(ID3D12Device* device, ID3D12CommandQueue* queue,
            std::shared_ptr<const Texture2D> texture, const std::filesystem::path& shaderPath);

        /// <summary>
        /// 深度テストあり・深度書き込みなしの加算合成で描画します。描画順によるソートは不要です。
        /// </summary>
        void Draw(ID3D12GraphicsCommandList* commands, const DirectX::XMFLOAT4X4& worldViewProjection,
            const std::array<float, 4>& color) const;

    private:
        /// <summary>
        /// ワールドビュー射影行列・画像・サンプラーを渡すルートシグネチャを生成します。
        /// </summary>
        /// <param name="device">生成に使用するデバイス。</param>
        /// <returns>生成に成功した場合は true。</returns>
        bool CreateRootSignature(ID3D12Device* device);

        /// <summary>
        /// シェーダーをコンパイルし、加算合成用パイプラインを生成します。
        /// </summary>
        /// <param name="device">生成に使用するデバイス。</param>
        /// <param name="shaderPath">コンパイルする HLSL ファイル。</param>
        /// <returns>生成に成功した場合は true。</returns>
        bool CreatePipelineState(ID3D12Device* device, const std::filesystem::path& shaderPath);

        /// <summary>
        /// 四頂点と六インデックスを DEFAULT ヒープへ転送し、完了を待機します。
        /// </summary>
        /// <param name="device">生成に使用するデバイス。</param>
        /// <param name="queue">転送に使用する DIRECT 型キュー。</param>
        /// <returns>生成と転送に成功した場合は true。</returns>
        bool CreateMeshBuffer(ID3D12Device* device, ID3D12CommandQueue* queue);

        bool initialized_ = false;
        std::shared_ptr<const Texture2D> texture_;
        Microsoft::WRL::ComPtr<ID3D12Resource> meshBuffer_;
        D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};
        D3D12_INDEX_BUFFER_VIEW indexBufferView_{};
        Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;
    };
}
