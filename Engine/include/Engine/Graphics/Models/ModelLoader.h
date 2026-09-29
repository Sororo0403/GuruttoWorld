#pragma once

#include <Engine/Graphics/Models/MeshData.h>

namespace Engine
{
    class ModelLoader final
    {
    public:
        /// <summary>
        /// Assimp で OBJ を読み込み、左手座標系の三角形メッシュへ変換します。法線がなければ生成します。
        /// </summary>
        /// <param name="path">OBJ ファイル。MTL と画像の相対パスはモデルのフォルダーを基準に解決します。</param>
        /// <param name="meshes">メッシュの出力先。成功した場合のみ置き換えます。</param>
        /// <returns>一つ以上の三角形メッシュを読み込めた場合は true。</returns>
        static bool Load(const std::filesystem::path& path, std::vector<MeshData>& meshes);
    };
}
