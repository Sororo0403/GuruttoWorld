#pragma once

#include <Engine/Graphics/Models/MeshData.h>

namespace Engine
{
    class ModelLoader final
    {
    public:
        /// <summary>
        /// Assimp 対応の静的モデルを左手座標系の三角形メッシュへ変換します。ノード変換を頂点に適用し、法線がなければ生成します。
        /// </summary>
        /// <param name="path">モデルファイル。外部テクスチャの相対パスはモデルのフォルダーを基準に解決します。埋め込みテクスチャは非対応です。</param>
        /// <param name="meshes">メッシュの出力先。成功した場合のみ置き換えます。</param>
        /// <returns>一つ以上の三角形メッシュを読み込めた場合は true。</returns>
        static bool Load(const std::filesystem::path& path, std::vector<MeshData>& meshes);
    };
}
