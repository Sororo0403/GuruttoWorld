#pragma once

#include <Engine/Graphics/Texture2D.h>
#include <array>

namespace Engine
{
    struct SpriteDrawParameters
    {
        // 画面左上を原点とするピクセル座標と、回転前の幅・高さです。
        std::array<float, 2> position{ 0.0f, 0.0f };
        std::array<float, 2> size{ 128.0f, 128.0f };
        // 中心を軸とする時計回りの回転角度（ラジアン）です。
        float rotation = 0.0f;
        std::array<float, 4> color{ 1.0f, 1.0f, 1.0f, 1.0f };
        // 画像を切り出す UV 範囲（左、上、右、下）です。
        std::array<float, 4> uvRect{ 0.0f, 0.0f, 1.0f, 1.0f };
    };

    class SpriteRenderer final
    {
    public:
        /// <summary>
        /// スプライト描画リソースの管理を初期化します。
        /// </summary>
        SpriteRenderer() = default;

        /// <summary>
        /// 描画リソースを解放します。事前に利用中の GPU 処理を完了させてください。
        /// </summary>
        ~SpriteRenderer() = default;

        /// <summary>
        /// リソースの二重所有を防ぐため、コピー生成を禁止します。
        /// </summary>
        SpriteRenderer(const SpriteRenderer&) = delete;

        /// <summary>
        /// リソースの二重所有を防ぐため、コピー代入を禁止します。
        /// </summary>
        SpriteRenderer& operator=(const SpriteRenderer&) = delete;

        /// <summary>
        /// スプライトのパイプラインとテクスチャを生成し、初期転送の完了を待機します。
        /// </summary>
        /// <param name="device">生成に使用するデバイス。</param>
        /// <param name="queue">テクスチャ転送に使用する DIRECT 型キュー。</param>
        /// <param name="texturePath">通常のアルファ形式の画像ファイル。</param>
        /// <param name="shaderPath">スプライト用の VSMain・PSMain を定義した HLSL ファイル。</param>
        /// <returns>生成に成功した場合は true。初期化済みの場合は false。</returns>
        bool Initialize(ID3D12Device* device, ID3D12CommandQueue* queue,
            const std::filesystem::path& texturePath, const std::filesystem::path& shaderPath);

        /// <summary>
        /// 深度判定・深度書き込みなしで、アルファ合成した四角形を描画します。後の描画ほど手前になります。
        /// </summary>
        /// <param name="commands">シーンと同じ描画先・ビューポート・シザーが設定済みのコマンドリスト。</param>
        /// <param name="viewportWidth">描画領域の幅（ピクセル）。</param>
        /// <param name="viewportHeight">描画領域の高さ（ピクセル）。</param>
        /// <param name="parameters">位置、サイズ、回転、色、UV 範囲。</param>
        void Draw(ID3D12GraphicsCommandList* commands, UINT viewportWidth, UINT viewportHeight,
            const SpriteDrawParameters& parameters) const;

    private:
        /// <summary>
        /// 画面座標・画像・サンプラーを渡すルートシグネチャを生成します。
        /// </summary>
        /// <param name="device">生成に使用するデバイス。</param>
        /// <returns>生成に成功した場合は true。</returns>
        bool CreateRootSignature(ID3D12Device* device);

        /// <summary>
        /// シェーダーをコンパイルし、アルファ合成用パイプラインを生成します。
        /// </summary>
        /// <param name="device">生成に使用するデバイス。</param>
        /// <param name="shaderPath">コンパイルする HLSL ファイル。</param>
        /// <returns>生成に成功した場合は true。</returns>
        bool CreatePipelineState(ID3D12Device* device, const std::filesystem::path& shaderPath);

        bool initialized_ = false;
        Texture2D texture_;
        Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;
    };
}
