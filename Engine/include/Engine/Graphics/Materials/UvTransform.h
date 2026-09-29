#pragma once

#include <array>

namespace Engine
{
    struct UvTransform
    {
        std::array<float, 2> scale{ 1.0f, 1.0f };
        // UV 原点を中心に回転します。単位はラジアンです。
        float rotation = 0.0f;
        std::array<float, 2> translation{ 0.0f, 0.0f };

        /// <summary>
        /// 拡縮・回転・移動の順に適用する二行のアフィン変換を、シェーダー用に生成します。
        /// </summary>
        /// <returns>各行を四要素に揃えた UV 変換定数。</returns>
        std::array<float, 8> GetConstants() const;
    };
}
