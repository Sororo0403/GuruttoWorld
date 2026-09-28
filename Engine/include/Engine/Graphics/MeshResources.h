#pragma once

#include <Engine/Graphics/Texture2D.h>
#include <Engine/Graphics/MeshData.h>
#include <map>
#include <memory>

namespace Engine
{
    class MeshResources final
    {
    public:
        /// <summary>
        /// 同一モデル内で共有するパイプラインとテクスチャの所有者を生成します。
        /// </summary>
        MeshResources() = default;
        /// <summary>
        /// GPU 完了後に共有リソースを解放します。
        /// </summary>
        ~MeshResources() = default;
        /// <summary>
        /// 所有者のコピー生成を禁止します。
        /// </summary>
        MeshResources(const MeshResources&) = delete;
        /// <summary>
        /// 所有者のコピー代入を禁止します。
        /// </summary>
        MeshResources& operator=(const MeshResources&) = delete;

        /// <summary>
        /// シェーダーを一度だけコンパイルし、共有ルートシグネチャと PSO を生成します。
        /// </summary>
        /// <param name="device">生成に使用するデバイス。</param>
        /// <param name="shaderPath">共通メッシュ用シェーダー。</param>
        /// <returns>初期化に成功した場合は true。</returns>
        bool Initialize(ID3D12Device* device, const std::filesystem::path& shaderPath);

        /// <summary>
        /// 正規化したパスに対応する画像を共有します。未登録の場合のみ転送し、完了を待機します。
        /// </summary>
        /// <param name="device">初期化時と同じデバイス。</param>
        /// <param name="queue">転送に使用する DIRECT 型キュー。</param>
        /// <param name="path">画像のパス。空の場合は共有の白テクスチャ。</param>
        /// <returns>共有テクスチャ。失敗時は空。</returns>
        std::shared_ptr<Texture2D> GetTexture(ID3D12Device* device, ID3D12CommandQueue* queue,
            const std::filesystem::path& path);

        /// <summary>
        /// 所有権を移譲せず共有ルートシグネチャを取得します。
        /// </summary>
        ID3D12RootSignature* GetRootSignature() const noexcept;
        /// <summary>
        /// 所有権を移譲せず共有 PSO を取得します。
        /// </summary>
        ID3D12PipelineState* GetPipelineState() const noexcept;

    private:
        struct PathLess
        {
            /// <summary>
            /// Windows のパスを大文字・小文字を区別せず比較します。
            /// </summary>
            bool operator()(const std::filesystem::path& left, const std::filesystem::path& right) const;
        };

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

        Microsoft::WRL::ComPtr<ID3D12Device> device_;
        Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
        Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;
        std::map<std::filesystem::path, std::shared_ptr<Texture2D>, PathLess> textures_;
    };
}
