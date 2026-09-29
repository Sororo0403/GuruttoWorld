#pragma once

#if defined(_DEBUG) || defined(ENGINE_DEVELOPMENT)
#include <Engine/Graphics/Materials/DirectionalLight.h>

namespace App
{
    class LightingPanel final
    {
    public:
        /// <summary>
        /// 平行光源の方向・色・強度と環境光・ハイライトを操作するパネルを構築します。
        /// </summary>
        /// <param name="light">編集する光源と反射の設定。</param>
        static void Draw(Engine::DirectionalLight& light);
    };
}
#endif
