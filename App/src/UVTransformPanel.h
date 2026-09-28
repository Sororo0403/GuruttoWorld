#pragma once

#if defined(_DEBUG)
#include <Engine/Graphics/UVTransform.h>

namespace App
{
    class UVTransformPanel final
    {
    public:
        /// <summary>
        /// 球体とスプライトの UV 拡縮・回転・移動を操作するパネルを構築します。
        /// </summary>
        /// <param name="sphere">球体の UV 変換。</param>
        /// <param name="sprite">スプライトの UV 変換。</param>
        static void Draw(Engine::UVTransform& sphere, Engine::UVTransform& sprite);
    };
}
#endif
