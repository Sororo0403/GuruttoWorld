#pragma once
#include <array>
#include <cmath>
#include <optional>

namespace Editor
{
    // Scene パネルの描画領域を画面座標で表します。タイトルバーやタブなどの操作領域は含めません。
    struct SceneViewport
    {
        float x = 0, y = 0, width = 0, height = 0;
        /// <summary>指定した値が許容範囲内かを確認します。</summary>
        bool Valid() const;
        bool Contains(float screenX, float screenY) const
        {
            return Valid() && screenX >= x && screenY >= y && screenX < x + width && screenY < y + height;
        }
        /// <summary>有効な矩形の幅と高さの比を返します。Valid が true の場合だけ呼び出してください。</summary>
        float Aspect() const
        {
            return width / height;
        }
        /// <summary>座標を正規化デバイス座標へ変換します。</summary>
        std::optional<std::array<float, 2>> ToNdc(float screenX, float screenY) const;
        std::array<float, 2> ToScreen(float ndcX, float ndcY) const
        {
            return {x + (ndcX + 1) * 0.5f * width, y + (1 - ndcY) * 0.5f * height};
        }
    };
}
