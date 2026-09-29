#pragma once

#if defined(_DEBUG) || defined(ENGINE_DEVELOPMENT)
#include <Engine/Graphics/Materials/UvTransform.h>

namespace App
{
    class UvTransformPanel final
    {
    public:
        /// <summary>
        /// モデルとスプライトの UV 拡縮・回転・移動を操作するパネルを構築します。
        /// </summary>
        /// <param name="model">モデルの UV 変換。</param>
        /// <param name="sprite">スプライトの UV 変換。</param>
        static void Draw(Engine::UvTransform& model, Engine::UvTransform& sprite);
    };
}
#endif
