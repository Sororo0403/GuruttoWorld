#pragma once

#if defined(_DEBUG)
#include <Engine/Graphics/UVTransform.h>

namespace App
{
    class UVTransformPanel final
    {
    public:
        /// <summary>
        /// モデルとスプライトの UV 拡縮・回転・移動を操作するパネルを構築します。
        /// </summary>
        /// <param name="model">モデルの UV 変換。</param>
        /// <param name="sprite">スプライトの UV 変換。</param>
        static void Draw(Engine::UVTransform& model, Engine::UVTransform& sprite);
    };
}
#endif
