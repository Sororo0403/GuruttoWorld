#pragma once

#include <Engine/Graphics/Resources/Texture2D.h>
#include <map>
#include <memory>

namespace Engine
{
    class TextureManager final
    {
    public:
        /// <summary>
        /// テクスチャの共有キャッシュを生成します。操作は描画スレッドで行ってください。
        /// </summary>
        TextureManager() = default;

        /// <summary>
        /// キャッシュの所有権を解放します。利用中の GPU 処理を完了させてから破棄してください。
        /// スプライトなどが保持する共有テクスチャは、最後の所有者が破棄されるまで保持されます。
        /// </summary>
        ~TextureManager() = default;

        /// <summary>
        /// キャッシュのコピー生成を禁止します。
        /// </summary>
        TextureManager(const TextureManager&) = delete;

        /// <summary>
        /// キャッシュのコピー代入を禁止します。
        /// </summary>
        TextureManager& operator=(const TextureManager&) = delete;

        /// <summary>
        /// 同一デバイスの DIRECT キューを登録します。初期化済みの場合は false を返します。
        /// </summary>
        bool Initialize(ID3D12Device* device, ID3D12CommandQueue* queue);

        /// <summary>
        /// 同じ正規化パスのテクスチャを共有します。初回だけファイルを読み込み、GPU 転送の完了を待機します。
        /// 失敗時は空を返し、キャッシュに登録しません。空のパスは共有の白い一画素を生成します。
        /// Windows の通常のパスとして大文字・小文字を区別せず扱います。
        /// </summary>
        std::shared_ptr<const Texture2D> Load(const std::filesystem::path& path);

    private:
        struct PathLess
        {
            /// <summary>
            /// パスを Windows の大文字・小文字を区別しない順序で比較します。
            /// </summary>
            bool operator()(const std::filesystem::path& left, const std::filesystem::path& right) const;
        };

        Microsoft::WRL::ComPtr<ID3D12Device> device_;
        Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue_;
        std::map<std::filesystem::path, std::shared_ptr<const Texture2D>, PathLess> textures_;
        std::map<std::filesystem::path,std::pair<std::filesystem::file_time_type,std::filesystem::file_time_type>,PathLess> textureStamps_;
    };
}
