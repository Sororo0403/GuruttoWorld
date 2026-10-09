#pragma once

#include <Engine/Graphics/Renderers/ModelRenderer.h>
#include <map>
#include <memory>
#include <functional>

namespace Engine
{
    class ModelManager final
    {
    public:
        /// <summary>
        /// モデルの共有キャッシュを生成します。操作は描画スレッドで行ってください。
        /// </summary>
        ModelManager() = default;

        /// <summary>
        /// キャッシュの所有権を解放します。利用中の GPU 処理を完了させてから破棄してください。
        /// オブジェクトなどが保持する共有モデルは、最後の所有者が破棄されるまで保持されます。
        /// </summary>
        ~ModelManager() = default;

        /// <summary>
        /// キャッシュのコピー生成を禁止します。
        /// </summary>
        ModelManager(const ModelManager&) = delete;

        /// <summary>
        /// キャッシュのコピー代入を禁止します。
        /// </summary>
        ModelManager& operator=(const ModelManager&) = delete;

        /// <summary>
        /// 同一デバイスの DIRECT キューを登録します。初期化済みの場合は false を返します。
        /// </summary>
        bool Initialize(ID3D12Device* device, ID3D12CommandQueue* queue, const std::filesystem::path& shaderPath);

        /// <summary>
        /// 同じ正規化パスのモデルを共有します。初回だけファイルを読み込み、GPU 転送の完了を待機します。
        /// 失敗時や空のパスは空を返し、キャッシュに登録しません。切り替え後もキャッシュがモデルを保持します。
        /// Windows の通常のパスとして大文字・小文字を区別せず扱います。
        /// </summary>
        std::shared_ptr<const ModelRenderer> Load(const std::filesystem::path& path);
        /// <summary>内容が変わった手続きメッシュだけ再生成し、IDごとの最新モデルを共有します。</summary>
        std::shared_ptr<const ModelRenderer> Procedural(const std::string& key,const std::string& signature,const std::function<std::vector<MeshData>()>& generate);

        // Swap complete caches only after GPU idle. Existing shared models keep their ownership.
        void Swap(ModelManager& other) noexcept;

    private:
        struct PathLess
        {
            /// <summary>
            /// パスを Windows の大文字・小文字を区別しない順序で比較します。
            /// </summary>
            bool operator()(const std::filesystem::path& left, const std::filesystem::path& right) const;
        };

        struct ProceduralModel {std::string signature;std::shared_ptr<const ModelRenderer> model;};
        std::map<std::string,ProceduralModel> procedural_;
        Microsoft::WRL::ComPtr<ID3D12Device> device_;
        Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue_;
        std::filesystem::path shaderPath_;
        std::map<std::filesystem::path, std::shared_ptr<const ModelRenderer>, PathLess> models_;
    };
}
